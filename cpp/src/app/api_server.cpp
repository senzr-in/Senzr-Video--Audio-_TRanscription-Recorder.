#include "app/api_server.hpp"
#include "app/ntp_client.hpp"
#include "app/gpio_manager.hpp"
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <algorithm>

namespace app {

using json = nlohmann::json;

// ── Network helpers ──────────────────────────────────────────────────────────

static std::string get_host_name() {
    char host[256] = {0};
    if (gethostname(host, sizeof(host)) == 0 && host[0] != '\0')
        return std::string(host);
    return "edge-gateway";
}

static std::string get_lan_ip() {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0)
        return "127.0.0.1";

    std::string result = "127.0.0.1";
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (std::string(ifa->ifa_name) == "lo") continue;

        char ip[INET_ADDRSTRLEN] = {0};
        auto* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) {
            result = ip;
            break;
        }
    }
    freeifaddrs(ifaddr);
    return result;
}

static bool has_camera_device() {
    if (std::filesystem::exists("/dev/video0")) return true;
    if (std::filesystem::exists("/dev/v4l/by-id")) {
        for (const auto& entry :
             std::filesystem::directory_iterator("/dev/v4l/by-id")) {
            (void)entry;
            return true;
        }
    }
    return false;
}

// ── ApiServer ─────────────────────────────────────────────────────────────────

ApiServer::ApiServer() {
    ota_updater_ = std::make_shared<app::OtaUpdater>();
}

// ── JSON helpers ─────────────────────────────────────────────────────────────

json ApiServer::status_json() const {
    auto cfg = config_manager_.read_config();
    return {
        {"status",           "online"},
        {"hostname",         get_host_name()},
        {"ip_address",       get_lan_ip()},
        {"timezone",         cfg.timezone.empty() ? NtpClient::get_system_timezone() : cfg.timezone},
        {"system_time",      NtpClient::get_local_timestamp()},
        {"config_loaded",    true},
        {"pipeline_running", pipeline_.is_running()},
        {"camera_connected", has_camera_device()}
    };
}

json ApiServer::config_json() const {
    auto cfg = config_manager_.read_config();
    return {
        {"devicename",          cfg.devicename},
        {"mode",                cfg.mode},
        {"timezone",            cfg.timezone.empty() ? "Asia/Kolkata" : cfg.timezone},
        {"wifissid",            cfg.wifissid},
        {"wifipassword",        cfg.wifipassword},
        {"provisioningenabled", cfg.provisioningenabled},
        {"cameraconnected",     cfg.cameraconnected}
    };
}

// ── Route implementations ────────────────────────────────────────────────────

StatusResponse ApiServer::get_status() const {
    StatusResponse out;
    out.status           = "online";
    out.hostname         = get_host_name();
    out.ip_address       = get_lan_ip();
    out.config_loaded    = true;
    out.pipeline_running = pipeline_.is_running();
    return out;
}

ConfigModel ApiServer::get_config() const {
    return config_manager_.read_config();
}

ConfigModel ApiServer::update_config(const ConfigModel& config) {
    config_manager_.write_config(config);
    return config;
}

std::string ApiServer::camera_on() {
    if (pipeline_.is_running()) return "Pipeline already running";
    pipeline_.start();
    return "Pipeline started";
}

std::string ApiServer::camera_off() {
    if (!pipeline_.is_running()) return "Pipeline already stopped";
    pipeline_.stop();
    return "Pipeline stopped";
}

json ApiServer::camera_status_json() const {
    // Matches the fields app.js reads:
    // camera_enabled, is_recording, current_recording_file, last_recorded_file
    bool running   = pipeline_.is_running();
    bool recording = pipeline_.is_recording();
    return {
        {"pipeline_running",       running},
        {"initialized",            true},
        {"camera_enabled",         running},
        {"is_recording",           recording},
        {"current_recording_file", pipeline_.current_recording_file()},
        {"last_recorded_file",     pipeline_.last_recorded_file()}
    };
}

namespace {

static float read_sys_temp(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return 0.0f;
    long raw = 0;
    if (f >> raw) {
        return static_cast<float>(raw) / 1000.0f;
    }
    return 0.0f;
}

static void read_sys_mem(int& used_mb, int& total_mb) {
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) {
        used_mb = 0;
        total_mb = 0;
        return;
    }
    std::string key;
    long val = 0;
    std::string unit;
    long total_kb = 0, avail_kb = 0;
    while (f >> key >> val >> unit) {
        if (key == "MemTotal:") total_kb = val;
        else if (key == "MemAvailable:") avail_kb = val;
    }
    total_mb = static_cast<int>(total_kb / 1024);
    used_mb  = static_cast<int>((total_kb - avail_kb) / 1024);
}

static int read_sys_uptime() {
    std::ifstream f("/proc/uptime");
    if (!f.is_open()) return 0;
    double sec = 0.0;
    if (f >> sec) return static_cast<int>(sec);
    return 0;
}

static void read_sys_disk(float& used_gb, float& total_gb) {
    try {
        auto space = std::filesystem::space("/");
        total_gb = static_cast<float>(space.capacity) / (1024.0f * 1024.0f * 1024.0f);
        used_gb  = static_cast<float>(space.capacity - space.available) / (1024.0f * 1024.0f * 1024.0f);
    } catch (...) {
        total_gb = 0.0f;
        used_gb  = 0.0f;
    }
}

static uint64_t get_dir_size_and_count(const std::filesystem::path& dir, int& count) {
    uint64_t total = 0;
    count = 0;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec)) {
            total += entry.file_size(ec);
            count++;
        }
    }
    return total;
}

} // namespace

// ── Route registration ───────────────────────────────────────────────────────

void ApiServer::register_routes(httplib::Server& srv) {

    // GET /api/status
    srv.Get("/api/status", [this](const httplib::Request&,
                                   httplib::Response& res) {
        res.set_content(status_json().dump(2), "application/json");
    });

    // GET /api/config
    srv.Get("/api/config", [this](const httplib::Request&,
                                   httplib::Response& res) {
        res.set_content(config_json().dump(2), "application/json");
    });

    // POST /api/config
    srv.Post("/api/config", [this](const httplib::Request& req,
                                    httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            res.status = 400;
            res.set_content(R"({"error":"invalid json"})", "application/json");
            return;
        }
        ConfigModel cfg;
        cfg.devicename          = body.value("devicename",          "");
        cfg.mode                = body.value("mode",                "object");
        cfg.timezone            = body.value("timezone",            "Asia/Kolkata");
        cfg.wifissid            = body.value("wifissid",            "");
        cfg.wifipassword        = body.value("wifipassword",        "");
        cfg.provisioningenabled = body.value("provisioningenabled", false);
        cfg.cameraconnected     = body.value("cameraconnected",     false);

        if (!cfg.timezone.empty()) {
            NtpClient::set_system_timezone(cfg.timezone);
            NtpClient::write_to_hardware_rtc();
        }

        update_config(cfg);
        res.set_content(config_json().dump(2), "application/json");
    });

    // ── Time & Timezone Endpoints ────────────────────────────────────────────
    srv.Get("/api/system/time", [this](const httplib::Request&, httplib::Response& res) {
        auto cfg = config_manager_.read_config();
        std::string tz = cfg.timezone.empty() ? NtpClient::get_system_timezone() : cfg.timezone;
        res.set_content(json{
            {"timezone",            tz},
            {"local_time",          NtpClient::get_local_timestamp()},
            {"utc_time",            NtpClient::get_iso8601_timestamp()},
            {"available_timezones", NtpClient::get_available_timezones()}
        }.dump(2), "application/json");
    });

    srv.Post("/api/system/timezone", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("timezone")) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Timezone is required"})", "application/json");
            return;
        }

        std::string tz = body.value("timezone", "Asia/Kolkata");
        bool ok = NtpClient::set_system_timezone(tz);
        if (ok) {
            auto cfg = config_manager_.read_config();
            cfg.timezone = tz;
            config_manager_.write_config(cfg);
            NtpClient::write_to_hardware_rtc();
        }

        res.set_content(json{
            {"ok", ok},
            {"timezone", tz},
            {"local_time", NtpClient::get_local_timestamp()},
            {"message", ok ? "Timezone updated successfully" : "Failed to update timezone"}
        }.dump(), "application/json");
    });

    srv.Post("/api/system/time/sync", [this](const httplib::Request&, httplib::Response& res) {
        NtpClient ntp;
        bool ok = ntp.sync_with_fallbacks();
        res.set_content(json{
            {"ok", ok},
            {"local_time", NtpClient::get_local_timestamp()},
            {"message", ok ? "NTP clock synchronized and written to RTC" : "NTP sync failed"}
        }.dump(), "application/json");
    });

    // POST /api/camera/on
    srv.Post("/api/camera/on", [this](const httplib::Request&,
                                       httplib::Response& res) {
        std::string msg = camera_on();
        bool ok = true;
        res.set_content(json{{"ok", ok}, {"message", msg}}.dump(),
                        "application/json");
    });

    // POST /api/camera/off
    srv.Post("/api/camera/off", [this](const httplib::Request&,
                                        httplib::Response& res) {
        std::string msg = camera_off();
        res.set_content(json{{"ok", true}, {"message", msg}}.dump(),
                        "application/json");
    });

    // GET /api/camera/status
    srv.Get("/api/camera/status", [this](const httplib::Request&,
                                          httplib::Response& res) {
        res.set_content(camera_status_json().dump(), "application/json");
    });

    // ── Wi-Fi Endpoints ──────────────────────────────────────────────────────
    srv.Get("/api/wifi/status", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(connectivity_manager_.wifi_status_json().dump(2), "application/json");
    });

    srv.Get("/api/wifi/scan", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(connectivity_manager_.wifi_scan_json().dump(2), "application/json");
    });

    srv.Post("/api/wifi/connect", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("ssid")) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"SSID is required"})", "application/json");
            return;
        }
        std::string ssid = body.value("ssid", "");
        std::string password = body.value("password", "");
        std::string err;
        bool ok = connectivity_manager_.connect_wifi(ssid, password, err);
        if (ok) {
            auto cfg = config_manager_.read_config();
            cfg.wifissid = ssid;
            cfg.wifipassword = password;
            config_manager_.write_config(cfg);
        }
        res.set_content(json{{"ok", ok}, {"message", ok ? "Connected" : err}}.dump(), "application/json");
    });

    srv.Post("/api/wifi/disconnect", [this](const httplib::Request&, httplib::Response& res) {
        bool ok = connectivity_manager_.disconnect_wifi();
        res.set_content(json{{"ok", ok}}.dump(), "application/json");
    });

    // ── Ethernet Endpoints ───────────────────────────────────────────────────
    srv.Get("/api/ethernet/status", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(connectivity_manager_.ethernet_status_json().dump(2), "application/json");
    });

    srv.Post("/api/ethernet/config", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Invalid JSON payload"})", "application/json");
            return;
        }

        std::string mode = body.value("mode", "dhcp");
        std::string iface = body.value("interface_name", "");
        std::string err;
        bool ok = false;

        if (mode == "dhcp") {
            ok = connectivity_manager_.set_ethernet_dhcp(iface, err);
        } else if (mode == "static") {
            std::string ip = body.value("ip_address", "");
            std::string prefix = body.value("prefix_cidr", "24");
            std::string gateway = body.value("gateway", "");
            std::string dns = body.value("dns", "8.8.8.8");

            if (ip.empty()) {
                res.status = 400;
                res.set_content(R"({"ok":false,"error":"IP address is required for static configuration"})", "application/json");
                return;
            }

            std::string ip_with_prefix = ip;
            if (ip_with_prefix.find('/') == std::string::npos && !prefix.empty()) {
                ip_with_prefix += "/" + prefix;
            }

            ok = connectivity_manager_.set_ethernet_static_ip(iface, ip_with_prefix, gateway, dns, err);
        } else {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Unsupported mode. Use 'dhcp' or 'static'"})", "application/json");
            return;
        }

        res.set_content(json{
            {"ok", ok},
            {"mode", mode},
            {"message", ok ? "Ethernet settings applied successfully" : err}
        }.dump(), "application/json");
    });

    // ── Bluetooth Endpoints ──────────────────────────────────────────────────
    srv.Get("/api/bluetooth/status", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(connectivity_manager_.bluetooth_status_json().dump(2), "application/json");
    });

    srv.Post("/api/bluetooth/power", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        bool enable = body.value("enabled", true);
        bool ok = connectivity_manager_.set_bluetooth_power(enable);
        res.set_content(json{{"ok", ok}, {"enabled", enable}}.dump(), "application/json");
    });

    srv.Get("/api/bluetooth/scan", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(connectivity_manager_.bluetooth_scan_json().dump(2), "application/json");
    });

    srv.Post("/api/bluetooth/pair", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        std::string addr = body.value("address", "");
        std::string err;
        bool ok = connectivity_manager_.pair_bluetooth_device(addr, err);
        res.set_content(json{{"ok", ok}, {"message", ok ? "Paired" : err}}.dump(), "application/json");
    });

    // ── GPIO & Access Control Endpoints ─────────────────────────────────────
    srv.Get("/api/gpio/pins", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(gpio_manager_.pins_json().dump(2), "application/json");
    });

    srv.Post("/api/gpio/write", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Invalid JSON"})", "application/json");
            return;
        }

        int val = body.value("value", 0);
        PinValue pval = (val == 1) ? PinValue::HIGH : PinValue::LOW;
        bool ok = false;

        if (body.contains("alias")) {
            std::string alias = body.value("alias", "");
            ok = gpio_manager_.write_alias(alias, pval);
        } else if (body.contains("pin")) {
            int pin = body.value("pin", -1);
            ok = gpio_manager_.digital_write(pin, pval);
        }

        res.set_content(json{{"ok", ok}, {"value", val}}.dump(), "application/json");
    });

    srv.Get("/api/gpio/read", [this](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("alias")) {
            std::string alias = req.get_param_value("alias");
            PinValue v = gpio_manager_.read_alias(alias);
            res.set_content(json{{"alias", alias}, {"value", static_cast<int>(v)}}.dump(), "application/json");
            return;
        }
        if (req.has_param("pin")) {
            int pin = std::stoi(req.get_param_value("pin"));
            PinValue v = gpio_manager_.digital_read(pin);
            res.set_content(json{{"pin", pin}, {"value", static_cast<int>(v)}}.dump(), "application/json");
            return;
        }
        res.status = 400;
        res.set_content(R"({"error":"Missing 'pin' or 'alias' parameter"})", "application/json");
    });

    srv.Post("/api/gpio/door/unlock", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        int duration_ms = body.is_object() ? body.value("duration_ms", 3000) : 3000;
        
        bool ok = false;
        if (body.is_object() && body.contains("pin")) {
            int pin = body.value("pin", 7);
            ok = gpio_manager_.pulse(pin, duration_ms);
        } else {
            ok = gpio_manager_.pulse_alias("DOOR_RELAY", duration_ms);
        }

        res.set_content(json{
            {"ok", ok},
            {"duration_ms", duration_ms},
            {"message", ok ? "Door unlock pulse triggered" : "Failed to pulse relay"}
        }.dump(), "application/json");
    });

    srv.Post("/api/gpio/buzzer", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        int beeps = body.is_object() ? body.value("beeps", 2) : 2;
        int duration_ms = body.is_object() ? body.value("duration_ms", 100) : 100;
        
        bool ok = false;
        if (body.is_object() && body.contains("pin")) {
            int pin = body.value("pin", 11);
            ok = gpio_manager_.buzzer_beep(pin, beeps, duration_ms, 100);
        } else {
            ok = gpio_manager_.buzzer_beep_alias("BUZZER", beeps, duration_ms, 100);
        }

        res.set_content(json{
            {"ok", ok},
            {"beeps", beeps},
            {"message", ok ? "Buzzer beep triggered" : "Failed to trigger buzzer"}
        }.dump(), "application/json");
    });

    // ── Live Vision & AI Testing Endpoints ────────────────────────────────────

    srv.Get("/api/vision/stream", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-cache, private");
        res.set_header("Pragma", "no-cache");
        res.set_content_provider(
            "multipart/x-mixed-replace; boundary=frame",
            [this](size_t /*offset*/, httplib::DataSink& sink) {
                if (!pipeline_.is_running()) {
                    sink.done();
                    return false;
                }
                auto jpeg = pipeline_.get_latest_preview_jpeg();
                if (jpeg.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    return true;
                }
                std::string header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                                     std::to_string(jpeg.size()) + "\r\n\r\n";
                std::string footer = "\r\n";
                sink.write(header.data(), header.size());
                sink.write(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
                sink.write(footer.data(), footer.size());
                std::this_thread::sleep_for(std::chrono::milliseconds(40)); // ~25 FPS
                return true;
            }
        );
    });

    srv.Get("/api/vision/snapshot", [this](const httplib::Request&, httplib::Response& res) {
        auto jpeg = pipeline_.get_latest_preview_jpeg();
        if (jpeg.empty()) {
            res.status = 503;
            res.set_content(R"({"error":"No preview frame available yet"})", "application/json");
            return;
        }
        res.set_content(reinterpret_cast<const char*>(jpeg.data()), jpeg.size(), "image/jpeg");
    });

    srv.Get("/api/vision/detections", [this](const httplib::Request&, httplib::Response& res) {
        auto meta = pipeline_.get_latest_detections_json();
        res.set_content(meta.empty() ? "{}" : meta.dump(), "application/json");
    });

    srv.Get("/api/vision/mode", [this](const httplib::Request&, httplib::Response& res) {
        auto meta = pipeline_.get_latest_detections_json();
        std::string mode = meta.value("mode", "both");
        res.set_content(json{{"mode", mode}}.dump(), "application/json");
    });

    srv.Post("/api/vision/mode", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (!body.is_object() || !body.contains("mode")) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing 'mode' (must be 'person', 'plate', or 'both')\"}", "application/json");
            return;
        }
        std::string mode = body["mode"].get<std::string>();
        if (mode != "person" && mode != "plate" && mode != "both") {
            res.status = 400;
            res.set_content("{\"error\":\"Invalid mode. Use 'person', 'plate', or 'both'\"}", "application/json");
            return;
        }
        pipeline_.set_vision_mode(mode);
        res.set_content(json{{"ok", true}, {"mode", mode}}.dump(), "application/json");
    });

    // ── Offline Storage & Detection Events Endpoints ──────────────────────────

    // GET /api/system/storage — Deep hierarchical storage inspection
    srv.Get("/api/system/storage", [this](const httplib::Request&, httplib::Response& res) {
        float disk_used_gb = 0.0f, disk_total_gb = 0.0f;
        read_sys_disk(disk_used_gb, disk_total_gb);
        float disk_free_gb = std::max(0.0f, disk_total_gb - disk_used_gb);
        float disk_used_pct = disk_total_gb > 0 ? (disk_used_gb / disk_total_gb) * 100.0f : 0.0f;

        // DB file size
        uint64_t db_bytes = 0;
        std::error_code ec;
        for (const auto& f : {"database/edge_gateway.db", "database/edge_gateway.db-wal", "database/edge_gateway.db-shm"}) {
            if (std::filesystem::exists(f, ec)) {
                db_bytes += std::filesystem::file_size(f, ec);
            }
        }
        float db_mb = static_cast<float>(db_bytes) / (1024.0f * 1024.0f);

        int rec_count = 0;
        uint64_t rec_bytes = get_dir_size_and_count("/opt/edge-gateway/recordings", rec_count);
        float rec_mb = static_cast<float>(rec_bytes) / (1024.0f * 1024.0f);

        int plate_count = 0;
        uint64_t plate_bytes = get_dir_size_and_count("/opt/edge-gateway/recordings/plates", plate_count);
        float plate_mb = static_cast<float>(plate_bytes) / (1024.0f * 1024.0f);

        int person_count = 0;
        uint64_t person_bytes = get_dir_size_and_count("/opt/edge-gateway/recordings/persons", person_count);
        float person_mb = static_cast<float>(person_bytes) / (1024.0f * 1024.0f);

        json j = {
            {"root_disk", {
                {"total_gb", disk_total_gb},
                {"used_gb", disk_used_gb},
                {"free_gb", disk_free_gb},
                {"used_pct", disk_used_pct}
            }},
            {"database", {
                {"size_bytes", db_bytes},
                {"size_mb", db_mb},
                {"path", "database/edge_gateway.db"},
                {"total_sessions", database_.get_row_count("recording_sessions")},
                {"total_detection_events", database_.get_row_count("detection_events")},
                {"total_system_events", database_.get_row_count("system_events")},
                {"total_telemetry_rows", database_.get_row_count("device_telemetry")}
            }},
            {"recordings", {
                {"video_sessions", {
                    {"count", rec_count},
                    {"size_mb", rec_mb},
                    {"path", "/opt/edge-gateway/recordings"}
                }},
                {"plates", {
                    {"count", plate_count},
                    {"size_mb", plate_mb},
                    {"path", "/opt/edge-gateway/recordings/plates"}
                }},
                {"persons", {
                    {"count", person_count},
                    {"size_mb", person_mb},
                    {"path", "/opt/edge-gateway/recordings/persons"}
                }}
            }}
        };
        res.set_content(j.dump(), "application/json");
    });

    // GET /api/system/hardware — Real-time RK3588 hardware metrics
    srv.Get("/api/system/hardware", [this](const httplib::Request&, httplib::Response& res) {
        float cpu_temp = read_sys_temp("/sys/class/thermal/thermal_zone0/temp");
        float npu_temp = read_sys_temp("/sys/class/thermal/thermal_zone2/temp");
        float gpu_temp = read_sys_temp("/sys/class/thermal/thermal_zone3/temp");

        int used_mb = 0, total_mb = 0;
        read_sys_mem(used_mb, total_mb);
        int free_mb = std::max(0, total_mb - used_mb);
        float ram_used_pct = total_mb > 0 ? (static_cast<float>(used_mb) / total_mb) * 100.0f : 0.0f;

        double loads[3] = {0.0, 0.0, 0.0};
        getloadavg(loads, 3);
        int cores = std::thread::hardware_concurrency();

        int uptime_sec = read_sys_uptime();
        int days = uptime_sec / 86400;
        int hours = (uptime_sec % 86400) / 3600;
        int mins = (uptime_sec % 3600) / 60;
        int secs = uptime_sec % 60;
        std::ostringstream uph;
        if (days > 0) uph << days << "d ";
        if (hours > 0 || days > 0) uph << hours << "h ";
        uph << mins << "m " << secs << "s";

        json j = {
            {"cpu_temp_c", cpu_temp},
            {"npu_temp_c", npu_temp},
            {"gpu_temp_c", gpu_temp},
            {"ram", {
                {"used_mb", used_mb},
                {"total_mb", total_mb},
                {"free_mb", free_mb},
                {"used_pct", ram_used_pct}
            }},
            {"cpu_load", {
                {"load_1m", loads[0]},
                {"load_5m", loads[1]},
                {"load_15m", loads[2]},
                {"cores", cores}
            }},
            {"uptime_sec", uptime_sec},
            {"uptime_human", uph.str()},
            {"camera_active", pipeline_.is_running()},
            {"recording_active", pipeline_.is_recording()}
        };
        res.set_content(j.dump(), "application/json");
    });

    // GET /api/plates/gallery — List captured license plate snapshots
    srv.Get("/api/plates/gallery", [](const httplib::Request& req, httplib::Response& res) {
        int limit = 60;
        if (req.has_param("limit")) {
            try { limit = std::stoi(req.get_param_value("limit")); } catch (...) {}
        }
        std::filesystem::path dir("/opt/edge-gateway/recordings/plates");
        std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::directory_entry>> files;
        std::error_code ec;
        if (std::filesystem::exists(dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".jpg") {
                    files.push_back({entry.last_write_time(ec), entry});
                }
            }
        }
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });

        json list = json::array();
        int count = 0;
        for (const auto& item : files) {
            if (++count > limit) break;
            const auto& entry = item.second;
            std::string fname = entry.path().filename().string();
            std::string plate_text = "";
            if (fname.rfind("plate_", 0) == 0) {
                auto first_under = fname.find('_');
                auto second_under = fname.find('_', first_under + 1);
                if (first_under != std::string::npos && second_under != std::string::npos) {
                    plate_text = fname.substr(first_under + 1, second_under - first_under - 1);
                }
            }
            list.push_back({
                {"filename", fname},
                {"url", "/api/plates/snapshot/" + fname},
                {"plate_text", plate_text},
                {"size_bytes", entry.file_size(ec)}
            });
        }
        res.set_content(list.dump(), "application/json");
    });

    // GET /api/persons/gallery — List captured person detection snapshots
    srv.Get("/api/persons/gallery", [](const httplib::Request& req, httplib::Response& res) {
        int limit = 60;
        if (req.has_param("limit")) {
            try { limit = std::stoi(req.get_param_value("limit")); } catch (...) {}
        }
        std::filesystem::path dir("/opt/edge-gateway/recordings/persons");
        std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::directory_entry>> files;
        std::error_code ec;
        if (std::filesystem::exists(dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".jpg") {
                    files.push_back({entry.last_write_time(ec), entry});
                }
            }
        }
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });

        json list = json::array();
        int count = 0;
        for (const auto& item : files) {
            if (++count > limit) break;
            const auto& entry = item.second;
            std::string fname = entry.path().filename().string();
            list.push_back({
                {"filename", fname},
                {"url", "/api/persons/snapshot/" + fname},
                {"size_bytes", entry.file_size(ec)}
            });
        }
        res.set_content(list.dump(), "application/json");
    });

    // POST /api/system/storage/clean — Disk space reclamation
    srv.Post("/api/system/storage/clean", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        std::string target = body.is_object() ? body.value("target", "") : "";
        int deleted_files = 0;
        uint64_t freed_bytes = 0;
        std::error_code ec;

        auto clean_dir = [&](const std::string& path_str) {
            std::filesystem::path p(path_str);
            if (std::filesystem::exists(p, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
                    if (entry.is_regular_file(ec)) {
                        auto sz = entry.file_size(ec);
                        if (std::filesystem::remove(entry.path(), ec)) {
                            deleted_files++;
                            freed_bytes += sz;
                        }
                    }
                }
            }
        };

        if (target == "plates" || target == "all") {
            clean_dir("/opt/edge-gateway/recordings/plates");
        }
        if (target == "persons" || target == "all") {
            clean_dir("/opt/edge-gateway/recordings/persons");
        }
        if (target == "vacuum_db") {
            database_.vacuum_old_events(1);
            database_.vacuum_old_telemetry(1);
        }

        res.set_content(json{
            {"ok", true},
            {"target", target},
            {"deleted_files", deleted_files},
            {"freed_bytes", freed_bytes},
            {"freed_mb", static_cast<float>(freed_bytes) / (1024.0f * 1024.0f)}
        }.dump(), "application/json");
    });

    // GET /api/plates/snapshot/:filename
    srv.Get(R"(/api/plates/snapshot/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string fname = req.matches[1];
        std::filesystem::path p = std::filesystem::path("/opt/edge-gateway/recordings/plates") / fname;
        if (!std::filesystem::exists(p)) {
            res.status = 404;
            res.set_content("Snapshot not found", "text/plain");
            return;
        }
        std::ifstream ifs(p, std::ios::binary);
        if (ifs.is_open()) {
            std::stringstream ss;
            ss << ifs.rdbuf();
            res.set_content(ss.str(), "image/jpeg");
        } else {
            res.status = 500;
        }
    });

    // GET /api/persons/snapshot/:filename
    srv.Get(R"(/api/persons/snapshot/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string fname = req.matches[1];
        std::filesystem::path p = std::filesystem::path("/opt/edge-gateway/recordings/persons") / fname;
        if (!std::filesystem::exists(p)) {
            res.status = 404;
            res.set_content("Snapshot not found", "text/plain");
            return;
        }
        std::ifstream ifs(p, std::ios::binary);
        if (ifs.is_open()) {
            std::stringstream ss;
            ss << ifs.rdbuf();
            res.set_content(ss.str(), "image/jpeg");
        } else {
            res.status = 500;
        }
    });

    // GET /api/events/detections?type=PLATE_DETECTED&limit=100
    srv.Get("/api/events/detections", [this](const httplib::Request& req, httplib::Response& res) {
        int limit = 100;
        std::string type_filter;
        if (req.has_param("limit")) {
            try { limit = std::stoi(req.get_param_value("limit")); } catch (...) {}
        }
        if (req.has_param("type")) {
            type_filter = req.get_param_value("type");
        }
        res.set_content(database_.get_detection_events_json(limit, type_filter), "application/json");
    });

    // GET /api/events/system?limit=100
    srv.Get("/api/events/system", [this](const httplib::Request& req, httplib::Response& res) {
        int limit = 100;
        if (req.has_param("limit")) {
            try { limit = std::stoi(req.get_param_value("limit")); } catch (...) {}
        }
        res.set_content(database_.get_system_events_json(limit), "application/json");
    });

    // GET /api/telemetry?n=60
    srv.Get("/api/telemetry", [this](const httplib::Request& req, httplib::Response& res) {
        int n = 60;
        if (req.has_param("n")) {
            try { n = std::stoi(req.get_param_value("n")); } catch (...) {}
        }
        res.set_content(database_.get_telemetry_json(n), "application/json");
    });

    // GET /api/sessions?limit=50
    srv.Get("/api/sessions", [this](const httplib::Request& req, httplib::Response& res) {
        int limit = 50;
        if (req.has_param("limit")) {
            try { limit = std::stoi(req.get_param_value("limit")); } catch (...) {}
        }
        res.set_content(database_.get_sessions_json(limit), "application/json");
    });

    // GET /api/sessions/pending
    srv.Get("/api/sessions/pending", [this](const httplib::Request&, httplib::Response& res) {
        auto jobs = database_.get_pending_upload_jobs();
        nlohmann::json j = nlohmann::json::array();
        for (const auto& job : jobs) {
            j.push_back({
                {"id", job.id},
                {"session_id", job.session_id},
                {"file_path", job.file_path},
                {"file_type", job.file_type},
                {"status", job.status},
                {"attempts", job.attempts}
            });
        }
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/sessions/:id/retry
    srv.Post(R"(/api/sessions/([^/]+)/retry)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string session_id = req.matches[1];
        bool ok = database_.update_session_upload_status(session_id, "pending", "", "manual retry");
        res.set_content(json{{"ok", ok}, {"session_id", session_id}}.dump(), "application/json");
    });

    // ── MQTT Access Control Integration Endpoints ───────────────────────────
    srv.Get("/api/mqtt/status", [this](const httplib::Request&, httplib::Response& res) {
        auto cfg = mqtt_client_.get_config();
        res.set_content(json{
            {"connected", mqtt_client_.is_connected()},
            {"uuid", cfg.uuid},
            {"broker", cfg.broker_host},
            {"port", cfg.broker_port},
            {"prefix", cfg.prefix},
            {"device_name", cfg.device_name},
            {"model", cfg.device_model},
            {"heartbeat_sec", cfg.heartbeat_interval_sec}
        }.dump(2), "application/json");
    });

    srv.Post("/api/mqtt/handshake", [this](const httplib::Request&, httplib::Response& res) {
        bool ok = mqtt_client_.publish_connect_handshake();
        res.set_content(json{{"ok", ok}, {"message", ok ? "Connect handshake published" : "Failed to publish"}}.dump(), "application/json");
    });

    // ── FOTA Endpoints ───────────────────────────────────────────────────────
    srv.Get("/api/fota/status", [this](const httplib::Request&, httplib::Response& res) {
        if (!ota_updater_) {
            res.status = 503;
            res.set_content(R"({"error":"updater not initialized"})", "application/json");
            return;
        }
        json out = {
            {"current_version", ota_updater_->get_current_version()},
            {"device_id",       ota_updater_->get_config().device_id},
            {"is_updating",     ota_updater_->is_updating()},
            {"s3_bucket",       ota_updater_->get_config().s3_bucket},
            {"mqtt_broker",     ota_updater_->get_config().mqtt_broker},
            {"mqtt_topic",      ota_updater_->get_config().mqtt_topic}
        };
        res.set_content(out.dump(2), "application/json");
    });

    srv.Post("/api/fota/check", [this](const httplib::Request&, httplib::Response& res) {
        if (!ota_updater_) {
            res.status = 503;
            res.set_content(R"({"error":"updater not initialized"})", "application/json");
            return;
        }
        auto meta = ota_updater_->fetch_latest_metadata();
        if (meta) {
            json out = {
                {"success", true},
                {"latest_version", meta->version},
                {"current_version", ota_updater_->get_current_version()},
                {"update_available", meta->version != ota_updater_->get_current_version()},
                {"file", meta->file},
                {"checksum", meta->checksum},
                {"download_url", meta->download_url},
                {"size_bytes", meta->size_bytes}
            };
            res.set_content(out.dump(2), "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"success":false,"error":"Unable to fetch latest release metadata from S3"})", "application/json");
        }
    });

    srv.Post("/api/fota/trigger", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ota_updater_) {
            res.status = 503;
            res.set_content(R"({"error":"updater not initialized"})", "application/json");
            return;
        }
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            res.status = 400;
            res.set_content(R"({"error":"invalid json"})", "application/json");
            return;
        }
        std::string version = body.value("version", "");
        bool reboot = body.value("reboot", false);
        std::string url = body.value("url", body.value("download_url", ""));
        uint64_t size_bytes = body.value("size_bytes", 0ULL);

        if (version.empty()) {
            auto meta = ota_updater_->fetch_latest_metadata();
            if (meta) {
                version = meta->version;
                if (url.empty()) url = meta->download_url;
                if (size_bytes == 0) size_bytes = meta->size_bytes;
            } else {
                res.status = 400;
                res.set_content(R"({"error":"version required and could not be determined automatically"})", "application/json");
                return;
            }
        }

        bool started = ota_updater_->trigger_async_update(version, reboot, url, size_bytes);
        json out = {
            {"success", started},
            {"version", version},
            {"reboot", reboot},
            {"message", started ? "Update worker started" : "Update already running or failed to start"}
        };
        res.set_content(out.dump(2), "application/json");
    });

    // ── Static frontend files resolution ──
    std::string frontend_dir = "frontend";
    if (std::filesystem::exists("frontend/index.html")) {
        frontend_dir = "frontend";
    } else if (std::filesystem::exists("../frontend/index.html")) {
        frontend_dir = "../frontend";
    } else if (std::filesystem::exists("/home/orangepi/edge-gateway/cpp/frontend/index.html")) {
        frontend_dir = "/home/orangepi/edge-gateway/cpp/frontend";
    } else {
        try {
            auto exe_path = std::filesystem::canonical("/proc/self/exe").parent_path();
            if (std::filesystem::exists(exe_path / "frontend" / "index.html")) {
                frontend_dir = (exe_path / "frontend").string();
            } else if (std::filesystem::exists(exe_path / ".." / "frontend" / "index.html")) {
                frontend_dir = (exe_path / ".." / "frontend").string();
            }
        } catch (...) {}
    }

    std::cout << "[edge-gateway] Mounting frontend static files from: " << frontend_dir << "\n";
    srv.set_mount_point("/", frontend_dir);

    // Explicit fallback handlers to guarantee index.html, style.css, and app.js are served
    srv.Get("/", [frontend_dir](const httplib::Request&, httplib::Response& res) {
        std::ifstream file(frontend_dir + "/index.html");
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            res.set_content(ss.str(), "text/html");
        } else {
            res.status = 404;
            res.set_content("<h1>404: frontend/index.html not found</h1>", "text/html");
        }
    });

    srv.Get("/style.css", [frontend_dir](const httplib::Request&, httplib::Response& res) {
        std::ifstream file(frontend_dir + "/style.css");
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            res.set_content(ss.str(), "text/css");
        } else {
            res.status = 404;
        }
    });

    srv.Get("/app.js", [frontend_dir](const httplib::Request&, httplib::Response& res) {
        std::ifstream file(frontend_dir + "/app.js");
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            res.set_content(ss.str(), "application/javascript");
        } else {
            res.status = 404;
        }
    });
}


// ── Server entry point ───────────────────────────────────────────────────────

int ApiServer::run() {
    database_.init_db();
    logger_.write_file_log(
        std::string(R"({"event":"api_server_start","timestamp":")") +
        LoggerUtils::get_timestamp() + "\"}");

    // Start AccessEasy MQTT client (Handshake connect, Heartbeats, Remote Door Control, FOTA)
    mqtt_client_.set_remote_control_handler([this](const std::string& action, int door_no, int duration_ms) {
        std::cout << "[edge-gateway] Remote door command received: action=" << action
                  << ", door=" << door_no << ", pulse=" << duration_ms << "ms\n";
        (void)door_no;
        return gpio_manager_.pulse_pin(7, duration_ms); // Physical pin 7 = DOOR_RELAY
    });

    mqtt_client_.set_ota_update_handler([this](const std::string& version,
                                               const std::string& url,
                                               bool reboot,
                                               uint64_t size_bytes) -> bool {
        std::cout << "[edge-gateway] [FOTA] Downlink OTA command received: version=" << version
                  << ", url=" << url << ", reboot=" << reboot << ", size=" << size_bytes << "\n";
        if (!ota_updater_) return false;
        return ota_updater_->trigger_async_update(version, reboot, url, size_bytes);
    });

    mqtt_client_.start();

    // Forward real-time ANPR plate detection events to AccessEasy platform
    pipeline_.set_plate_detected_callback([this](const std::string& plate, float conf, const std::string& snapshot_fname) {
        if (mqtt_client_.is_connected()) {
            std::string snapshot_url = "/api/plates/snapshot/" + snapshot_fname;
            mqtt_client_.publish_access_record("PLATE", plate, conf, snapshot_url, true);
        }
    });

    // Background hardware telemetry recording thread (every 60s)
    std::jthread telemetry_thread([this](std::stop_token st) {
        while (!st.stop_requested()) {
            TelemetryRow row;
            row.cpu_temp_c  = read_sys_temp("/sys/class/thermal/thermal_zone0/temp");
            row.npu_temp_c  = read_sys_temp("/sys/class/thermal/thermal_zone2/temp");
            row.gpu_temp_c  = read_sys_temp("/sys/class/thermal/thermal_zone3/temp");
            read_sys_mem(row.ram_used_mb, row.ram_total_mb);
            read_sys_disk(row.disk_used_gb, row.disk_total_gb);
            row.uptime_sec  = read_sys_uptime();
            row.camera_active = pipeline_.is_running();
            row.recording_active = pipeline_.is_recording();
            database_.insert_telemetry(row);

            // Publish heartbeat to AccessEasy broker
            if (mqtt_client_.is_connected()) {
                json hb_metrics = {
                    {"status", "online"},
                    {"uptime_sec", row.uptime_sec},
                    {"cpu_temp_c", row.cpu_temp_c},
                    {"npu_temp_c", row.npu_temp_c},
                    {"gpu_temp_c", row.gpu_temp_c},
                    {"ram_used_mb", row.ram_used_mb},
                    {"ram_total_mb", row.ram_total_mb},
                    {"disk_used_gb", row.disk_used_gb},
                    {"disk_total_gb", row.disk_total_gb},
                    {"camera_active", row.camera_active},
                    {"recording_active", row.recording_active}
                };
                mqtt_client_.publish_heartbeat(hb_metrics);
            }

            for (int i = 0; i < 60 && !st.stop_requested(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    });

    httplib::Server srv;
    register_routes(srv);

    auto mqtt_cfg = mqtt_client_.get_config();
    std::string cur_ver = ota_updater_ ? ota_updater_->get_current_version() : "1.0.0";
    std::cout << "\n======================================================\n";
    std::cout << "  🚀 SENZR EDGE GATEWAY & ACCESS CONTROLLER READY      \n";
    std::cout << "------------------------------------------------------\n";
    std::cout << "  🔑 DEVICE UUID    : " << mqtt_cfg.uuid << "\n";
    std::cout << "  📦 FIRMWARE VER   : " << cur_ver << "\n";
    std::cout << "  🌐 LAN IP         : " << get_lan_ip() << "\n";
    std::cout << "  📡 MQTT BROKER    : " << mqtt_cfg.broker_host << ":" << mqtt_cfg.broker_port << "\n";
    std::cout << "  📥 DOWNLINK TOPIC : " << mqtt_cfg.prefix << "/cmd/" << mqtt_cfg.uuid << "/#\n";
    std::cout << "  💻 LOCAL DASHBOARD: http://" << get_lan_ip() << ":8000\n";
    std::cout << "======================================================\n\n";

    srv.listen("0.0.0.0", 8000);
    return 0;
}

} // namespace app
