#include "app/updater.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <csignal>
#include <sys/types.h>
#include <sys/statvfs.h>
#if defined(__linux__)
#include <sys/reboot.h>
#endif

#include <curl/curl.h>
#include <openssl/evp.h>

#ifdef HAVE_MOSQUITTO
#include <mosquitto.h>
#endif

namespace app {

using json = nlohmann::json;

#include <algorithm>

#include <algorithm>

// ── Device ID (MAC Address) Auto-Resolution ──────────────────────────────────

static bool is_target_for_this_device(const std::string& target, const std::string& device_id) {
    if (target.empty() || target == "all" || target == device_id) return true;
    auto clean = [](std::string s) {
        std::string res;
        for (char c : s) {
            if (c != ':' && c != '-' && c != ' ') {
                res += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        }
        return res;
    };
    return clean(target) == clean(device_id);
}

std::string OtaUpdater::get_default_device_id() {
    // 1. Try reading MAC address of primary ethernet and wireless interfaces (eth0, end0, eth1, wlan0)
    for (const auto* iface : {"eth0", "end0", "eth1", "wlan0"}) {
        std::filesystem::path p = std::string("/sys/class/net/") + iface + "/address";
        if (std::filesystem::exists(p)) {
            std::ifstream f(p);
            std::string mac;
            if (std::getline(f, mac) && !mac.empty()) {
                auto start = mac.find_first_not_of(" \t\r\n");
                auto end   = mac.find_last_not_of(" \t\r\n");
                if (start != std::string::npos && end != std::string::npos) {
                    mac = mac.substr(start, end - start + 1);
                }
                if (!mac.empty() && mac != "00:00:00:00:00:00") {
                    return mac; // Device ID is directly the MAC address
                }
            }
        }
    }
    // 2. Scan all available network interfaces in /sys/class/net (excluding loopback)
    try {
        if (std::filesystem::exists("/sys/class/net")) {
            for (const auto& entry : std::filesystem::directory_iterator("/sys/class/net")) {
                std::string ifname = entry.path().filename().string();
                if (ifname == "lo") continue;
                std::filesystem::path p = entry.path() / "address";
                if (std::filesystem::exists(p)) {
                    std::ifstream f(p);
                    std::string mac;
                    if (std::getline(f, mac) && !mac.empty()) {
                        auto start = mac.find_first_not_of(" \t\r\n");
                        auto end   = mac.find_last_not_of(" \t\r\n");
                        if (start != std::string::npos && end != std::string::npos) {
                            mac = mac.substr(start, end - start + 1);
                        }
                        if (!mac.empty() && mac != "00:00:00:00:00:00") {
                            return mac;
                        }
                    }
                }
            }
        }
    } catch (...) {}

    return "00:00:00:00:00:00";
}

// ── Environment file parser ──────────────────────────────────────────────────

UpdaterConfig UpdaterConfig::load_from_env(const std::filesystem::path& env_file) {
    UpdaterConfig cfg;

    auto parse_env_file = [&](const std::filesystem::path& p) {
        if (!std::filesystem::exists(p)) return;
        std::ifstream file(p);
        std::string line;
        while (std::getline(file, line)) {
            // Trim leading whitespace
            auto start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line[start] == '#') continue;
            auto eq = line.find('=', start);
            if (eq == std::string::npos) continue;
            std::string key = line.substr(start, eq - start);
            std::string val = line.substr(eq + 1);
            // Trim whitespace/quotes
            auto v_start = val.find_first_not_of(" \t\r\n\"'");
            auto v_end   = val.find_last_not_of(" \t\r\n\"'");
            if (v_start != std::string::npos && v_end != std::string::npos) {
                val = val.substr(v_start, v_end - v_start + 1);
            } else {
                val = "";
            }
            // Set in process environment if not already set
            setenv(key.c_str(), val.c_str(), 0);
        }
    };

    parse_env_file(env_file);

    auto getenv_str = [](const char* name, const std::string& def) -> std::string {
        const char* v = std::getenv(name);
        return v ? std::string(v) : def;
    };

    auto getenv_int = [](const char* name, int def) -> int {
        const char* v = std::getenv(name);
        if (!v) return def;
        try { return std::stoi(v); } catch (...) { return def; }
    };

    cfg.device_id        = getenv_str("DEVICE_ID", "");
    if (cfg.device_id.empty()) {
        cfg.device_id    = OtaUpdater::get_default_device_id();
    }
    cfg.s3_bucket        = getenv_str("S3_BUCKET", "senzr-fota-storage");
    cfg.s3_prefix        = getenv_str("S3_PREFIX", "app-releases");
    cfg.s3_region        = getenv_str("S3_REGION", "us-east-1");
    cfg.mqtt_broker      = getenv_str("MQTT_BROKER", "mqtt.fieldseasy.com");
    cfg.mqtt_port        = getenv_int("MQTT_PORT", 1883);
    cfg.mqtt_topic       = getenv_str("MQTT_TOPIC", "edge-gateway/update");
    cfg.current_dir      = getenv_str("CURRENT_DIR", "/opt/edge-gateway/current");
    cfg.backup_dir       = getenv_str("BACKUP_DIR", "/opt/edge-gateway/backup");
    cfg.downloads_dir    = getenv_str("DOWNLOADS_DIR", "/opt/edge-gateway/downloads");
    cfg.check_interval_sec = getenv_int("CHECK_INTERVAL", 60);
    cfg.app_service      = getenv_str("APP_SERVICE", "edge-gateway");
    cfg.health_check_url = getenv_str("HEALTH_CHECK_URL", "http://127.0.0.1:8000/api/status");
    cfg.lock_file        = getenv_str("LOCK_FILE", "/tmp/edge-gateway-update.lock");
    cfg.reboot_on_success = (getenv_str("REBOOT_AFTER_UPDATE", "false") == "true");

    return cfg;
}

// ── Curl write callback ──────────────────────────────────────────────────────

namespace {
static size_t curl_string_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

static size_t curl_file_write_cb(void* ptr, size_t size, size_t nmemb, void* stream) {
    return std::fwrite(ptr, size, nmemb, static_cast<FILE*>(stream));
}
} // anonymous namespace

// ── Construction / Destruction ───────────────────────────────────────────────

OtaUpdater::OtaUpdater(UpdaterConfig config)
    : config_(std::move(config))
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
#ifdef HAVE_MOSQUITTO
    mosquitto_lib_init();
#endif
}

OtaUpdater::~OtaUpdater() {
    stop();
#ifdef HAVE_MOSQUITTO
    stop_mqtt();
    mosquitto_lib_cleanup();
#endif
    curl_global_cleanup();
}

// ── Logging helper ───────────────────────────────────────────────────────────

void OtaUpdater::log(const std::string& level, const std::string& msg) const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << " [" << level << "] " << msg;
    std::string line = oss.str();

    std::cout << line << std::endl;

    // Append to log file with 5MB rotation
    try {
        auto log_dir = config_.current_dir / "logs";
        std::filesystem::create_directories(log_dir);
        auto log_file = log_dir / "updater.log";
        if (std::filesystem::exists(log_file) && std::filesystem::file_size(log_file) > 5 * 1024 * 1024) {
            std::filesystem::rename(log_file, log_dir / "updater.log.old");
        }
        std::ofstream f(log_file, std::ios::app);
        if (f.is_open()) {
            f << line << '\n';
        }
    } catch (...) {}
}

// ── Version management ───────────────────────────────────────────────────────

std::string OtaUpdater::get_current_version() const {
    // 1. Try reading version.txt
    auto vtxt = config_.current_dir / "version.txt";
    if (std::filesystem::exists(vtxt)) {
        std::ifstream f(vtxt);
        std::string v;
        if (f >> v && !v.empty()) return v;
    }
    // 2. Try reading .version
    auto vdot = config_.current_dir / ".version";
    if (std::filesystem::exists(vdot)) {
        std::ifstream f(vdot);
        std::string v;
        if (f >> v && !v.empty()) return v;
    }
    return "0.0.0";
}

void OtaUpdater::set_current_version(const std::string& version) const {
    try {
        std::filesystem::create_directories(config_.current_dir);
        // Write both version.txt and .version
        std::ofstream f1(config_.current_dir / "version.txt");
        if (f1.is_open()) f1 << version;
        std::ofstream f2(config_.current_dir / ".version");
        if (f2.is_open()) f2 << version;

        std::cout << "\n======================================================\n";
        std::cout << "  🎉 SYSTEM FIRMWARE VERSION UPDATED TO: " << version << "\n";
        std::cout << "======================================================\n\n";
    } catch (...) {}
}

// ── Locking ──────────────────────────────────────────────────────────────────

bool OtaUpdater::acquire_lock() const {
    if (std::filesystem::exists(config_.lock_file)) {
        std::ifstream f(config_.lock_file);
        pid_t pid = 0;
        if (f >> pid && pid > 0) {
            // Check if PID is still alive
            if (::kill(pid, 0) != 0) {
                // Dead process -> clean stale lock
                std::error_code ec;
                std::filesystem::remove(config_.lock_file, ec);
            } else {
                log("WARNING", "Update already in progress — active PID: " + std::to_string(pid));
                return false;
            }
        } else {
            // Empty or invalid lock file
            std::error_code ec;
            std::filesystem::remove(config_.lock_file, ec);
        }
    }
    std::ofstream f(config_.lock_file);
    if (f.is_open()) {
        f << getpid();
        return true;
    }
    return false;
}

void OtaUpdater::release_lock() const {
    std::error_code ec;
    std::filesystem::remove(config_.lock_file, ec);
}

// ── SHA-256 Checksum ─────────────────────────────────────────────────────────

std::string OtaUpdater::sha256_of_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer))) {
        EVP_DigestUpdate(ctx, buffer, file.gcount());
    }
    if (file.gcount() > 0) {
        EVP_DigestUpdate(ctx, buffer, file.gcount());
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    EVP_DigestFinal_ex(ctx, hash, &length);
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < length; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// ── Service control ──────────────────────────────────────────────────────────

static void ensure_service_unit_installed(const std::string& service) {
    std::filesystem::path service_file = "/etc/systemd/system/" + service + ".service";
    if (std::filesystem::exists(service_file)) {
        return;
    }

    std::string unit_content;
    if (service == "edge-gateway" || service == "edge-gateway.service") {
        unit_content =
            "[Unit]\n"
            "Description=Edge Gateway C++ Core Service\n"
            "After=network.target\n\n"
            "[Service]\n"
            "Type=simple\n"
            "User=root\n"
            "WorkingDirectory=/opt/edge-gateway\n"
            "ExecStart=/opt/edge-gateway/bin/edge-gateway-backend\n"
            "Restart=always\n"
            "RestartSec=3\n"
            "KillMode=process\n\n"
            "[Install]\n"
            "WantedBy=multi-user.target\n";
    }

    if (!unit_content.empty()) {
        try {
            std::ofstream out(service_file);
            if (out.is_open()) {
                out << unit_content;
                out.close();
                std::system("systemctl --no-ask-password daemon-reload >/dev/null 2>&1 || sudo -n systemctl --no-ask-password daemon-reload >/dev/null 2>&1");
            }
        } catch (...) {}
    }
}

bool OtaUpdater::service_action(const std::string& action, const std::string& service) {
    // If stopping, check if the service is actually running under systemd
    if (action == "stop") {
        int check = std::system(("SYSTEMD_ASK_PASSWORD=0 systemctl --no-ask-password is-active --quiet " + service + " >/dev/null 2>&1").c_str());
        if (check != 0) {
            // Not running as a systemd unit (e.g. running interactively or already stopped) — skip stop
            return true;
        }
    }

    ensure_service_unit_installed(service);
    std::string cmd;
    if (::geteuid() == 0) {
        cmd = "SYSTEMD_ASK_PASSWORD=0 systemctl --no-ask-password " + action + " " + service + " >/dev/null 2>&1";
    } else {
        // Non-root: try passwordless sudo first, then systemctl directly with ask-password disabled
        cmd = "sudo -n systemctl --no-ask-password " + action + " " + service + " >/dev/null 2>&1 || SYSTEMD_ASK_PASSWORD=0 systemctl --no-ask-password " + action + " " + service + " >/dev/null 2>&1";
    }
    int ret = std::system(cmd.c_str());
    return (ret == 0);
}

// ── Health check ─────────────────────────────────────────────────────────────

bool OtaUpdater::health_check(int retries, int delay_sec) const {
    for (int i = 0; i < retries; ++i) {
        CURL* curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, config_.health_check_url.c_str());
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request
            CURLcode res = curl_easy_perform(curl);
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            curl_easy_cleanup(curl);

            if (res == CURLE_OK && http_code == 200) {
                log("INFO", "Health check passed.");
                return true;
            }
        }
        log("WARNING", "Health check attempt " + std::to_string(i + 1) + "/" +
                       std::to_string(retries) + " failed. Retrying...");
        std::this_thread::sleep_for(std::chrono::seconds(delay_sec));
    }
    return false;
}

// ── S3 Metadata & Download ───────────────────────────────────────────────────

std::optional<ReleaseMetadata> OtaUpdater::fetch_latest_metadata() const {
    std::string url = "https://" + config_.s3_bucket + ".s3." + config_.s3_region +
                      ".amazonaws.com/" + config_.s3_prefix + "/latest.json";

    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    std::string response_data;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_string_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    // Optional AWS SigV4
    const char* access_key = std::getenv("AWS_ACCESS_KEY_ID");
    const char* secret_key = std::getenv("AWS_SECRET_ACCESS_KEY");
    if (access_key && secret_key) {
        curl_easy_setopt(curl, CURLOPT_AWS_SIGV4, ("aws:amz:" + config_.s3_region + ":s3").c_str());
        std::string userpwd = std::string(access_key) + ":" + secret_key;
        curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200) {
        log("ERROR", "Failed to fetch latest.json: HTTP " + std::to_string(http_code) + " (curl=" + std::to_string(res) + ")");
        return std::nullopt;
    }

    try {
        auto j = json::parse(response_data);
        ReleaseMetadata meta;
        meta.version  = j.value("version", "0.0.0");
        meta.file     = j.value("file", "");
        meta.checksum = j.value("checksum", "");
        return meta;
    } catch (const std::exception& e) {
        log("ERROR", std::string("Failed to parse latest.json: ") + e.what());
        return std::nullopt;
    }
}

bool OtaUpdater::check_disk_space(uint64_t required_bytes) const {
    try {
        auto check_dir = config_.downloads_dir;
        if (!std::filesystem::exists(check_dir)) {
            check_dir = config_.downloads_dir.parent_path();
        }
        struct statvfs vfs{};
        if (statvfs(check_dir.string().c_str(), &vfs) == 0) {
            uint64_t available_bytes = static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
            // Require at least 3x the package size or minimum 100MB
            uint64_t needed = std::max<uint64_t>(required_bytes * 3, 100ULL * 1024 * 1024);
            if (available_bytes < needed) {
                log("ERROR", "Insufficient disk storage! Available: " +
                             std::to_string(available_bytes / (1024 * 1024)) + " MB, Needed: " +
                             std::to_string(needed / (1024 * 1024)) + " MB");
                return false;
            }
            log("INFO", "Disk space check passed: " +
                        std::to_string(available_bytes / (1024 * 1024)) + " MB available");
            return true;
        }
    } catch (...) {}
    return true;
}

bool OtaUpdater::download_release(const std::string& file_name, const std::filesystem::path& dest,
                                  const std::string& direct_url) const {
    std::string url = !direct_url.empty() ? direct_url :
                      ("https://" + config_.s3_bucket + ".s3." + config_.s3_region +
                       ".amazonaws.com/" + config_.s3_prefix + "/" + file_name);

    log("INFO", "Downloading " + (direct_url.empty() ? url : "[Pre-Signed S3 URL]") + " -> " + dest.string());

    // Try up to 3 attempts with offset resumption
    for (int attempt = 1; attempt <= 3; ++attempt) {
        uint64_t existing_bytes = 0;
        std::error_code ec;
        if (std::filesystem::exists(dest, ec)) {
            existing_bytes = std::filesystem::file_size(dest, ec);
            if (ec) existing_bytes = 0;
        }

        const char* open_mode = (existing_bytes > 0) ? "ab" : "wb";
        FILE* fp = std::fopen(dest.string().c_str(), open_mode);
        if (!fp) {
            log("ERROR", "Failed to open destination file for writing: " + dest.string());
            return false;
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            std::fclose(fp);
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L); // 5 min for large releases
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_file_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

        // Resume download from existing offset if previous attempt was interrupted
        if (existing_bytes > 0) {
            log("INFO", "Resuming download from byte offset: " + std::to_string(existing_bytes));
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(existing_bytes));
        }

        // Only attach SigV4 if NOT using a pre-signed URL
        if (direct_url.empty()) {
            const char* access_key = std::getenv("AWS_ACCESS_KEY_ID");
            const char* secret_key = std::getenv("AWS_SECRET_ACCESS_KEY");
            if (access_key && secret_key) {
                curl_easy_setopt(curl, CURLOPT_AWS_SIGV4, ("aws:amz:" + config_.s3_region + ":s3").c_str());
                std::string userpwd = std::string(access_key) + ":" + secret_key;
                curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
            }
        }

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);
        std::fclose(fp);

        if (res == CURLE_OK && (http_code == 200 || http_code == 206)) {
            log("INFO", "Download completed successfully on attempt " + std::to_string(attempt));
            return true;
        }

        log("WARNING", "Download attempt " + std::to_string(attempt) + "/3 failed: HTTP " +
                        std::to_string(http_code) + " (curl=" + std::to_string(res) + ")");
        if (attempt < 3) {
            std::this_thread::sleep_for(std::chrono::seconds(2 * attempt));
        }
    }

    log("ERROR", "All download attempts failed");
    return false;
}

bool OtaUpdater::publish_mqtt(const std::string& topic, const std::string& payload) const {
#ifdef HAVE_MOSQUITTO
    if (!mosq_) return false;
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    int ret = mosquitto_publish(static_cast<struct mosquitto*>(mosq_),
                                nullptr,
                                topic.c_str(),
                                static_cast<int>(payload.size()),
                                payload.c_str(),
                                1, // QoS 1
                                false);
    return (ret == MOSQ_ERR_SUCCESS);
#else
    (void)topic;
    (void)payload;
    return false;
#endif
}

void OtaUpdater::publish_status(const std::string& status, const std::string& message, const std::string& target_version) const {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");

    json j = {
        {"device_id", config_.device_id},
        {"deviceId", config_.device_id},
        {"mac", config_.device_id},
        {"status", status},
        {"current_version", get_current_version()},
        {"target_version", target_version.empty() ? get_current_version() : target_version},
        {"message", message},
        {"timestamp", oss.str()}
    };

    std::string topic = "edge-gateway/" + config_.device_id + "/status";
    publish_mqtt(topic, j.dump());
}

void OtaUpdater::publish_telemetry() const {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");

    json j = {
        {"device_id", config_.device_id},
        {"deviceId", config_.device_id},
        {"mac", config_.device_id},
        {"status", "online"},
        {"version", get_current_version()},
        {"timestamp", oss.str()}
    };

    std::string topic = "edge-gateway/" + config_.device_id + "/telemetry";
    publish_mqtt(topic, j.dump());
}

// ── Asynchronous Update Worker Management ────────────────────────────────────

bool OtaUpdater::trigger_async_update(const std::string& version, bool request_reboot,
                                      const std::string& direct_url, uint64_t size_bytes) {
    if (is_updating_.load()) {
        log("WARNING", "Update already running in this process — ignoring trigger for version " + version);
        return false;
    }

    std::lock_guard<std::mutex> lock(worker_mutex_);
    if (update_worker_ && update_worker_->joinable()) {
        update_worker_->join();
        update_worker_.reset();
    }

    update_worker_ = std::make_unique<std::thread>([this, version, request_reboot, direct_url, size_bytes]() {
        // run_update's internal RAII UpdateScopeGuard guarantees release_lock() and
        // is_updating_.store(false) execute even under exceptions — no manual cleanup here.
        this->run_update(version, request_reboot, direct_url, size_bytes);
    });

    return true;
}

// ── Core Update Routine ──────────────────────────────────────────────────────

bool OtaUpdater::run_update(const std::string& version, bool request_reboot,
                            const std::string& direct_url, uint64_t size_bytes) {
    bool expected = false;
    if (!is_updating_.compare_exchange_strong(expected, true)) {
        log("WARNING", "Update already running in this process — ignoring trigger");
        return false;
    }

    if (!acquire_lock()) {
        is_updating_.store(false);
        return false;
    }

    struct UpdateScopeGuard {
        const OtaUpdater* self;
        ~UpdateScopeGuard() {
            self->release_lock();
            const_cast<OtaUpdater*>(self)->is_updating_.store(false);
        }
    } scope_guard{this};

    // 0. Duplicate Update Prevention
    std::string current_ver = get_current_version();
    if (!version.empty() && version != "firmware_update" && version == current_ver) {
        log("INFO", "Device " + config_.device_id + " is already running target version " + version + " — duplicate update skipped.");
        publish_status("already_up_to_date", "Firmware version " + version + " is already running", version);
        return true;
    }

    log("INFO", "Starting OTA update: " + current_ver + " -> " + version + " for device " + config_.device_id +
                (request_reboot || config_.reboot_on_success ? " (with automatic reboot)" : ""));
    publish_status("downloading", "Starting firmware update download", version);

    // 0. Pre-flight storage check
    if (!check_disk_space(size_bytes)) {
        publish_status("failed", "Insufficient disk storage for update", version);
        return false;
    }

    auto meta = fetch_latest_metadata();
    std::string release_file = meta ? meta->file : (version + ".zip");
    std::string expected_sha = meta ? meta->checksum : "";
    if (meta && size_bytes == 0) size_bytes = meta->size_bytes;

    std::error_code dir_ec;
    std::filesystem::create_directories(config_.downloads_dir, dir_ec);
    if (dir_ec) {
        // Fallback to local working directory if system path is restricted
        config_.downloads_dir = std::filesystem::current_path() / "downloads";
        config_.backup_dir    = std::filesystem::current_path() / "backup";
        config_.current_dir   = std::filesystem::current_path() / "current";
        std::filesystem::create_directories(config_.downloads_dir, dir_ec);
    }
    auto zip_path = config_.downloads_dir / release_file;
    auto extract_dir = config_.downloads_dir / ("extract_" + version);

    bool update_success = false;
    try {
        // 1. Download
        if (!download_release(release_file, zip_path, direct_url)) {
            publish_status("failed", "Download failed", version);
            return false;
        }

        // 2. Verify SHA-256
        if (!expected_sha.empty()) {
            publish_status("verifying", "Verifying release SHA-256 checksum", version);
            std::string actual_sha = sha256_of_file(zip_path);
            if (actual_sha != expected_sha) {
                log("ERROR", "Checksum mismatch! expected=" + expected_sha + " actual=" + actual_sha);
                publish_status("failed", "Checksum mismatch: expected " + expected_sha + ", got " + actual_sha, version);
                std::error_code ec;
                std::filesystem::remove(zip_path, ec);
                return false;
            }
            log("INFO", "Checksum verified successfully (" + actual_sha + ")");
        }

        // 3. Extract release (100% Self-contained multi-tool extraction)
        publish_status("installing", "Extracting firmware package", version);
        std::filesystem::create_directories(extract_dir, dir_ec);

        std::string zpath = zip_path.string();
        std::string edir  = extract_dir.string();
        int ret = -1;

        // Try 1: standard system unzip
        std::string unzip_cmd = "unzip -q -o \"" + zpath + "\" -d \"" + edir + "\" 2>/dev/null";
        ret = std::system(unzip_cmd.c_str());

        // Try 2: built-in python3 zipfile module (standard on all Linux distros, 0 extra installs)
        if (ret != 0) {
            std::string py_cmd = "python3 -m zipfile -e \"" + zpath + "\" \"" + edir + "\" 2>/dev/null";
            ret = std::system(py_cmd.c_str());
        }

        // Try 3: tar for tar.gz/tar archives
        if (ret != 0) {
            std::string tar_cmd = "tar -xf \"" + zpath + "\" -C \"" + edir + "\" 2>/dev/null";
            ret = std::system(tar_cmd.c_str());
        }

        // Try 4: busybox unzip
        if (ret != 0) {
            std::string bb_cmd = "busybox unzip -q -o \"" + zpath + "\" -d \"" + edir + "\" 2>/dev/null";
            ret = std::system(bb_cmd.c_str());
        }

        if (ret != 0) {
            log("ERROR", "Failed to extract release package archive");
            publish_status("failed", "Failed to extract package archive", version);
            return false;
        }
        log("INFO", "Extracted release to " + extract_dir.string());

        // 4. Validate required files
        for (const auto& req : config_.required_files) {
            if (!std::filesystem::exists(extract_dir / req)) {
                log("WARNING", "Validation warning: missing " + req);
            }
        }

        // 5. Backup current version
        if (std::filesystem::exists(config_.backup_dir)) {
            std::filesystem::remove_all(config_.backup_dir);
        }
        if (std::filesystem::exists(config_.current_dir)) {
            std::filesystem::copy(config_.current_dir, config_.backup_dir,
                                  std::filesystem::copy_options::recursive);
            log("INFO", "Backup saved to " + config_.backup_dir.string());
        }

        // 6. Stop service
        log("INFO", "Stopping service: " + config_.app_service);
        service_action("stop", config_.app_service);
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // 7. Replace files
        for (const auto& entry : std::filesystem::directory_iterator(extract_dir)) {
            auto dest = config_.current_dir / entry.path().filename();
            if (std::filesystem::exists(dest)) {
                std::filesystem::remove_all(dest);
            }
            std::filesystem::copy(entry.path(), dest,
                                  std::filesystem::copy_options::recursive);
        }
        log("INFO", "Application files updated in " + config_.current_dir.string());

        // 8. Start new version
        log("INFO", "Starting service: " + config_.app_service);
        service_action("start", config_.app_service);
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // 9. Health check & rollback
        publish_status("testing", "Performing service health check", version);
        if (health_check()) {
            set_current_version(version);
            log("INFO", "Update to " + version + " successful!");
            publish_status("success", "Update installed successfully", version);
            publish_telemetry();
            std::filesystem::remove(zip_path);
            std::filesystem::remove_all(extract_dir);
            update_success = true;

            if (request_reboot || config_.reboot_on_success) {
                log("INFO", "Executing native hardware kernel reboot in 2 seconds...");
                publish_status("rebooting", "Update complete. System is rebooting...", version);
                std::this_thread::sleep_for(std::chrono::seconds(2));
                ::sync();
#if defined(__linux__)
                ::reboot(RB_AUTOBOOT);
#else
                std::system("reboot");
#endif
                return true;
            }
        } else {
            log("ERROR", "Health check failed! Initiating rollback to previous stable backup...");
            publish_status("rollback", "Health check failed, rolling back to previous version", version);
            service_action("stop", config_.app_service);
            if (std::filesystem::exists(config_.backup_dir)) {
                std::filesystem::remove_all(config_.current_dir);
                std::filesystem::copy(config_.backup_dir, config_.current_dir,
                                      std::filesystem::copy_options::recursive);
            }
            service_action("start", config_.app_service);
            std::filesystem::remove(zip_path);
            std::filesystem::remove_all(extract_dir);
            log("INFO", "Rollback completed. Restored previous stable version.");
        }

    } catch (const std::exception& e) {
        log("ERROR", std::string("Exception during update: ") + e.what());
        publish_status("failed", std::string("Exception during update: ") + e.what(), version);
    }

    return update_success;
}

// ── Periodic S3 Poll ─────────────────────────────────────────────────────────

void OtaUpdater::check_for_update() {
    auto meta = fetch_latest_metadata();
    if (!meta) return;

    std::string current_ver = get_current_version();
    log("INFO", "Version check for " + config_.device_id + ": current=" + current_ver + " available=" + meta->version);

    if (meta->version != current_ver && !meta->version.empty() && meta->version != "0.0.0") {
        log("INFO", "New version detected via S3 poll: " + meta->version);
        trigger_async_update(meta->version);
    }
}

void OtaUpdater::start_periodic_check() {
    periodic_thread_ = std::jthread([this](std::stop_token stoken) {
        log("INFO", "Periodic S3 check every " + std::to_string(config_.check_interval_sec) + "s started.");
        while (!stoken.stop_requested()) {
            for (int i = 0; i < config_.check_interval_sec && !stoken.stop_requested(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (!stoken.stop_requested()) {
                publish_telemetry();
                check_for_update();
            }
        }
    });
}

// ── MQTT Client Integration ──────────────────────────────────────────────────

#ifdef HAVE_MOSQUITTO
static void on_mqtt_connect(struct mosquitto* mosq, void* obj, int rc) {
    if (rc == 0) {
        auto* updater = static_cast<OtaUpdater*>(obj);
        std::cout << "[updater] MQTT connected successfully for device " << updater->get_config().device_id << ".\n";

        // 1. Subscribe to broadcast update topic
        mosquitto_subscribe(mosq, nullptr, updater->get_config().mqtt_topic.c_str(), 1);
        std::cout << "[updater] Subscribed to broadcast: " << updater->get_config().mqtt_topic << '\n';

        // 2. Subscribe to unicast device update topic
        std::string unicast_topic = "edge-gateway/" + updater->get_config().device_id + "/update";
        mosquitto_subscribe(mosq, nullptr, unicast_topic.c_str(), 1);
        std::cout << "[updater] Subscribed to unicast: " << unicast_topic << '\n';

        // 3. Publish initial online telemetry
        updater->publish_telemetry();
    } else {
        std::cerr << "[updater] MQTT connection failed with code: " << rc << '\n';
    }
}

static void on_mqtt_message(struct mosquitto* /*mosq*/, void* obj, const struct mosquitto_message* msg) {
    auto* updater = static_cast<OtaUpdater*>(obj);
    if (!msg || !msg->payload) return;

    std::string payload_str(static_cast<const char*>(msg->payload), msg->payloadlen);
    std::cout << "[updater] MQTT message received: " << payload_str << '\n';

    try {
        auto j = json::parse(payload_str);
        std::string action = j.value("action", j.value("type", ""));
        if (action == "update") {
            // Target device filter: only process if 'all' or matches this device MAC
            std::string target_dev = j.value("device_id", j.value("deviceId", j.value("mac", "all")));
            if (!is_target_for_this_device(target_dev, updater->get_config().device_id)) {
                std::cout << "[updater] Update ignored: targeted for device/MAC '" << target_dev
                          << "', this device MAC is '" << updater->get_config().device_id << "'\n";
                return;
            }

            std::string ver = j.value("version", "");
            bool do_reboot = j.value("reboot", false);
            std::string direct_url = j.value("url", j.value("download_url", j.value("presigned_url", "")));
            uint64_t size_bytes = j.value("size_bytes", j.value("size", 0ULL));
            if (!ver.empty()) {
                updater->trigger_async_update(ver, do_reboot, direct_url, size_bytes);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[updater] Failed to process MQTT message: " << e.what() << '\n';
    }
}
#endif

bool OtaUpdater::start_mqtt() {
#ifdef HAVE_MOSQUITTO
    std::string client_id = "edge-gateway-updater-" + config_.device_id + "-" + std::to_string(getpid());
    mosq_ = mosquitto_new(client_id.c_str(), true, this);
    if (!mosq_) {
        log("ERROR", "Failed to create Mosquitto MQTT client");
        return false;
    }

    mosquitto_connect_callback_set(static_cast<struct mosquitto*>(mosq_), on_mqtt_connect);
    mosquitto_message_callback_set(static_cast<struct mosquitto*>(mosq_), on_mqtt_message);

    int ret = mosquitto_connect_async(static_cast<struct mosquitto*>(mosq_),
                                      config_.mqtt_broker.c_str(),
                                      config_.mqtt_port,
                                      60);
    if (ret != MOSQ_ERR_SUCCESS) {
        log("ERROR", "mosquitto_connect_async failed: " + std::to_string(ret));
        return false;
    }

    mosquitto_loop_start(static_cast<struct mosquitto*>(mosq_));
    return true;
#else
    log("WARNING", "Built without Mosquitto MQTT support — running in periodic S3 polling mode");
    return false;
#endif
}

void OtaUpdater::stop_mqtt() {
#ifdef HAVE_MOSQUITTO
    if (mosq_) {
        mosquitto_loop_stop(static_cast<struct mosquitto*>(mosq_), true);
        mosquitto_destroy(static_cast<struct mosquitto*>(mosq_));
        mosq_ = nullptr;
    }
#endif
}

void OtaUpdater::run() {
    running_.store(true);
    log("INFO", "Edge Gateway C++ OTA Updater starting...");
    log("INFO", "Device ID: " + config_.device_id);
    log("INFO", "Current installed version: " + get_current_version());

    start_mqtt();
    start_periodic_check();
    check_for_update(); // Check immediately on boot

    log("INFO", "Updater running for device [" + config_.device_id + "]. Listening on broadcast [" +
                config_.mqtt_topic + "] & unicast [edge-gateway/" + config_.device_id + "/update].");

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void OtaUpdater::stop() {
    running_.store(false);
    if (periodic_thread_) {
        periodic_thread_->request_stop();
        periodic_thread_.reset();
    }
    stop_mqtt();
    std::lock_guard<std::mutex> lock(worker_mutex_);
    if (update_worker_ && update_worker_->joinable()) {
        update_worker_->join();
        update_worker_.reset();
    }
}

} // namespace app
