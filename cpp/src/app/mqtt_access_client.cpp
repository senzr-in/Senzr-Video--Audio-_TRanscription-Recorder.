#include "app/mqtt_access_client.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef HAVE_MOSQUITTO
#include <mosquitto.h>
#endif

namespace app {

using json = nlohmann::json;

#ifdef HAVE_MOSQUITTO
static void mqtt_c_connect_callback(struct mosquitto* /*mosq*/, void* obj, int rc) {
    if (obj) {
        static_cast<MqttAccessClient*>(obj)->on_connect(rc);
    }
}

static void mqtt_c_disconnect_callback(struct mosquitto* /*mosq*/, void* obj, int rc) {
    if (obj) {
        static_cast<MqttAccessClient*>(obj)->on_disconnect(rc);
    }
}

static void mqtt_c_message_callback(struct mosquitto* /*mosq*/, void* obj, const struct mosquitto_message* msg) {
    if (obj && msg && msg->topic && msg->payload) {
        std::string topic(msg->topic);
        std::string payload(static_cast<const char*>(msg->payload), msg->payloadlen);
        static_cast<MqttAccessClient*>(obj)->on_message(topic, payload);
    }
}
#endif

MqttAccessClient::MqttAccessClient(MqttAccessConfig config)
    : config_(std::move(config)) {
    init_device_uuid_if_empty();
#ifdef HAVE_MOSQUITTO
    mosquitto_lib_init();
#endif
}

MqttAccessClient::~MqttAccessClient() {
    stop();
#ifdef HAVE_MOSQUITTO
    mosquitto_lib_cleanup();
#endif
}

std::string MqttAccessClient::get_lan_ip() const {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) return "127.0.0.1";

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

void MqttAccessClient::init_device_uuid_if_empty() {
    std::lock_guard<std::mutex> lk(config_mutex_);
    if (!config_.uuid.empty()) return;

    // Try reading hardware MAC address of eth0 / end0 / wlan0
    for (const char* iface : {"eth0", "end0", "wlan0"}) {
        std::ifstream f(std::string("/sys/class/net/") + iface + "/address");
        if (f.is_open()) {
            std::string mac;
            if (f >> mac && mac.length() >= 12) {
                std::string clean_mac;
                for (char c : mac) {
                    if (c != ':') clean_mac += static_cast<char>(std::toupper(c));
                }
                config_.uuid = "DEVICE_UUID_" + clean_mac;
                return;
            }
        }
    }

    // Fallback: derive from IP
    std::string ip = get_lan_ip();
    std::string clean_ip;
    for (char c : ip) {
        if (c != '.') clean_ip += c;
    }
    config_.uuid = "DEVICE_UUID_" + clean_ip;
}

uint64_t MqttAccessClient::get_current_epoch_sec() const {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string MqttAccessClient::generate_serial(const std::string& prefix) const {
    static std::atomic<uint64_t> counter{1};
    std::ostringstream oss;
    oss << prefix << "_" << get_current_epoch_sec() << "_" << counter.fetch_add(1);
    return oss.str();
}

MqttAccessConfig MqttAccessClient::get_config() const {
    std::lock_guard<std::mutex> lk(config_mutex_);
    return config_;
}

void MqttAccessClient::update_config(const MqttAccessConfig& config) {
    std::lock_guard<std::mutex> lk(config_mutex_);
    config_ = config;
}

void MqttAccessClient::set_remote_control_handler(RemoteControlHandler handler) {
    remote_control_handler_ = std::move(handler);
}

void MqttAccessClient::set_config_update_handler(ConfigUpdateHandler handler) {
    config_update_handler_ = std::move(handler);
}

void MqttAccessClient::set_ota_update_handler(OtaUpdateHandler handler) {
    ota_update_handler_ = std::move(handler);
}

bool MqttAccessClient::start() {
    if (running_.load()) return true;

#ifndef HAVE_MOSQUITTO
    std::cerr << "[MQTT-Access] libmosquitto not compiled. MQTT client disabled.\n";
    return false;
#else
    MqttAccessConfig cfg = get_config();
    if (!cfg.enabled) {
        std::cout << "[MQTT-Access] MQTT access integration is disabled in config.\n";
        return false;
    }

    std::string client_id = cfg.uuid.empty() ? "DEVICE_UUID_DEFAULT" : cfg.uuid;
    mosq_ = mosquitto_new(client_id.c_str(), true, this);
    if (!mosq_) {
        std::cerr << "[MQTT-Access] Failed to create mosquitto client instance.\n";
        return false;
    }
#ifdef MOSQ_OPT_TCP_NODELAY
    mosquitto_int_option(mosq_, MOSQ_OPT_TCP_NODELAY, 1);
#endif

    // 1. Setup LWT (Last Will & Testament): access_device/v1/event/disconnect
    std::string lwt_topic = cfg.prefix + "/event/disconnect";
    json lwt_payload = {
        {"uuid", cfg.uuid},
        {"time", get_current_epoch_sec()}
    };
    std::string lwt_str = lwt_payload.dump();
    mosquitto_will_set(mosq_, lwt_topic.c_str(), static_cast<int>(lwt_str.length()),
                       lwt_str.c_str(), 1, false);

    // Callbacks
    mosquitto_connect_callback_set(mosq_, mqtt_c_connect_callback);
    mosquitto_disconnect_callback_set(mosq_, mqtt_c_disconnect_callback);
    mosquitto_message_callback_set(mosq_, mqtt_c_message_callback);

    // Authentication
    if (!cfg.username.empty()) {
        mosquitto_username_pw_set(mosq_, cfg.username.c_str(),
                                  cfg.password.empty() ? nullptr : cfg.password.c_str());
    }

    std::cout << "[MQTT-Access] Connecting to " << cfg.broker_host << ":" << cfg.broker_port
              << " as UUID: " << cfg.uuid << "...\n";

    int rc = mosquitto_connect_async(mosq_, cfg.broker_host.c_str(), cfg.broker_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT-Access] mosquitto_connect_async failed: " << rc << '\n';
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        return false;
    }

    mosquitto_loop_start(mosq_);
    running_.store(true);

    // 2. Start background Heartbeat Thread (runs every 30s)
    heartbeat_thread_ = std::jthread([this](std::stop_token st) {
        // Initial wait to let connection establish
        for (int i = 0; i < 5 && !st.stop_requested(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        while (!st.stop_requested()) {
            if (is_connected_.load()) {
                json hb_data = {
                    {"status", "online"},
                    {"ip", get_lan_ip()}
                };
                publish_heartbeat(hb_data);
            }

            int interval = get_config().heartbeat_interval_sec;
            if (interval <= 0) interval = 30;

            for (int i = 0; i < interval && !st.stop_requested(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    });

    return true;
#endif
}

void MqttAccessClient::stop() {
    running_.store(false);
    is_connected_.store(false);

    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.request_stop();
    }

#ifdef HAVE_MOSQUITTO
    if (mosq_) {
        mosquitto_disconnect(mosq_);
        mosquitto_loop_stop(mosq_, true);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
#endif
}

void MqttAccessClient::on_connect(int rc) {
#ifdef HAVE_MOSQUITTO
    if (rc == 0) {
        is_connected_.store(true);
        MqttAccessConfig cfg = get_config();
        std::cout << "\n[MQTT-Access] ==============================================\n";
        std::cout << "[MQTT-Access]  ✓ CONNECTED TO MQTT BROKER SUCCESSFULLY\n";
        std::cout << "[MQTT-Access]  🔑 DEVICE UUID: " << cfg.uuid << "\n";
        std::cout << "[MQTT-Access]  📡 BROKER     : " << cfg.broker_host << ":" << cfg.broker_port << "\n";
        std::cout << "[MQTT-Access] ==============================================\n";

        // 1. Mandatory Subscriptions:
        //    access_device/v1/cmd/{uuid}/#
        //    access_device/v1/event/{uuid}/#
        std::string cmd_sub = cfg.prefix + "/cmd/" + cfg.uuid + "/#";
        std::string evt_sub = cfg.prefix + "/event/" + cfg.uuid + "/#";

        mosquitto_subscribe(mosq_, nullptr, cmd_sub.c_str(), 1);
        mosquitto_subscribe(mosq_, nullptr, evt_sub.c_str(), 1);

        std::cout << "[MQTT-Access] 📥 Subscribed to downlink: " << cmd_sub << '\n';
        std::cout << "[MQTT-Access] 📤 Subscribed to replies:  " << evt_sub << '\n';

        // 2. Publish Handshake / Boot Announcement (connect)
        publish_connect_handshake();
    } else {
        is_connected_.store(false);
        std::cerr << "[MQTT-Access] Connection failed with error code: " << rc << '\n';
    }
#endif
}

void MqttAccessClient::on_disconnect(int rc) {
    is_connected_.store(false);
    std::cout << "[MQTT-Access] Disconnected from MQTT broker (rc: " << rc << ")\n";
}

bool MqttAccessClient::publish_custom(const std::string& topic, const nlohmann::json& payload, int qos) {
#ifdef HAVE_MOSQUITTO
    if (!mosq_ || !is_connected_.load()) return false;
    std::string s = payload.dump();
    int rc = mosquitto_publish(mosq_, nullptr, topic.c_str(),
                               static_cast<int>(s.length()), s.c_str(), qos, false);
    return rc == MOSQ_ERR_SUCCESS;
#else
    (void)topic; (void)payload; (void)qos;
    return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Protocol 2: Handshake / Boot Announcement (connect)
// ─────────────────────────────────────────────────────────────────────────────

bool MqttAccessClient::publish_connect_handshake() {
    MqttAccessConfig cfg = get_config();
    std::string topic = cfg.prefix + "/event/connect";

    json payload = {
        {"uuid", cfg.uuid},
        {"serialNo", generate_serial("boot")},
        {"time", get_current_epoch_sec()},
        {"sign", ""},
        {"data", {
            {"IP", get_lan_ip()},
            {"name", cfg.device_name},
            {"model", cfg.device_model},
            {"appVersion", cfg.app_version},
            {"sysVersion", cfg.sys_version}
        }}
    };

    std::cout << "[MQTT-Access] >>> Publishing Handshake Connect: " << topic << "\n"
              << "              Payload: " << payload.dump() << "\n";

    return publish_custom(topic, payload, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Protocol 4: Heartbeat Status
// ─────────────────────────────────────────────────────────────────────────────

bool MqttAccessClient::publish_heartbeat(const nlohmann::json& telemetry_data) {
    MqttAccessConfig cfg = get_config();
    std::string topic = cfg.prefix + "/event/heartbeat";

    json payload = {
        {"uuid", cfg.uuid},
        {"serialNo", generate_serial("hb")},
        {"time", get_current_epoch_sec()},
        {"data", telemetry_data}
    };

    return publish_custom(topic, payload, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Protocol 5: Real-time Access Record (ANPR plate / Person detection)
// ─────────────────────────────────────────────────────────────────────────────

bool MqttAccessClient::publish_access_record(const std::string& record_type,
                                             const std::string& identifier,
                                             float confidence,
                                             const std::string& snapshot_url,
                                             bool relay_triggered) {
    MqttAccessConfig cfg = get_config();
    std::string topic = cfg.prefix + "/event/access_record";

    json payload = {
        {"uuid", cfg.uuid},
        {"serialNo", generate_serial("rec")},
        {"time", get_current_epoch_sec()},
        {"data", {
            {"recordType", record_type},
            {"identifier", identifier},
            {"confidence", confidence},
            {"snapshotUrl", snapshot_url},
            {"relayTriggered", relay_triggered},
            {"direction", "IN"}
        }}
    };

    std::cout << "[MQTT-Access] >>> Publishing Access Record (" << identifier << ") to: " << topic << "\n";
    return publish_custom(topic, payload, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Downlink Message Dispatcher
// ─────────────────────────────────────────────────────────────────────────────

void MqttAccessClient::on_message(const std::string& topic, const std::string& payload_str) {
    std::cout << "[MQTT-Access] <<< Message received on [" << topic << "]: " << payload_str << "\n";

    // Ignore self-published replies to prevent echo loops
    if (topic.ends_with("/setConfig_reply") || topic.ends_with("/remoteControl_reply") || 
        topic.ends_with("/upgradeFirmware_reply") || topic.ends_with("/ota_reply")) {
        return;
    }

    try {
        auto j = json::parse(payload_str);
        std::string serial_no = j.value("serialNo", "");

        // 1. Automatic Provisioning: access_device/v1/cmd/{uuid}/setConfig
        if (topic.find("/cmd/") != std::string::npos && topic.ends_with("/setConfig")) {
            handle_cmd_set_config(serial_no, j);
            return;
        }

        // 2. Remote Control: access_device/v1/cmd/{uuid}/remoteControl
        if (topic.find("/cmd/") != std::string::npos && topic.ends_with("/remoteControl")) {
            handle_cmd_remote_control(serial_no, j);
            return;
        }

        // 3. Remote FOTA / OTA Upgrade: access_device/v1/cmd/{uuid}/upgradeFirmware or .../ota
        if (topic.find("/cmd/") != std::string::npos &&
            (topic.ends_with("/upgradeFirmware") ||
             topic.ends_with("/upgrade") ||
             topic.ends_with("/ota") ||
             topic.ends_with("/update") ||
             topic.ends_with("/firmware"))) {
            handle_cmd_ota(serial_no, j);
            return;
        }

        // Also check if payload has action/cmd == "ota" | "upgrade" | "update"
        std::string action = j.value("action", j.value("cmd", ""));
        if (action == "ota" || action == "upgrade" || action == "update" || action == "fota" || action == "firmwareUpgrade") {
            handle_cmd_ota(serial_no, j);
            return;
        }

        // 4. Connect Reply: access_device/v1/event/{uuid}/connect_reply
        if (topic.ends_with("/connect_reply")) {
            handle_event_connect_reply(j);
            return;
        }

    } catch (const std::exception& e) {
        std::cerr << "[MQTT-Access] Failed to parse message JSON: " << e.what() << '\n';
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Protocol 3: Automatic Provisioning (setConfig & setConfig_reply)
// ─────────────────────────────────────────────────────────────────────────────

void MqttAccessClient::handle_cmd_set_config(const std::string& serial_no, const nlohmann::json& payload) {
    MqttAccessConfig cfg = get_config();
    std::cout << "[MQTT-Access] Received setConfig command from platform.\n";

    bool broker_changed = false;
    if (payload.contains("data") && payload["data"].contains("mqttInfo")) {
        const auto& info = payload["data"]["mqttInfo"];
        std::string addr = info.value("mqttAddr", "");
        if (!addr.empty()) {
            auto colon = addr.find(':');
            std::string host = (colon != std::string::npos) ? addr.substr(0, colon) : addr;
            int port = (colon != std::string::npos) ? std::stoi(addr.substr(colon + 1)) : 1883;

            if (host != cfg.broker_host || port != cfg.broker_port) {
                cfg.broker_host = host;
                cfg.broker_port = port;
                broker_changed = true;
            }
        }
        cfg.username = info.value("mqttName", cfg.username);
        cfg.password = info.value("password", cfg.password);
        cfg.prefix   = info.value("prefix", cfg.prefix);
        update_config(cfg);

        if (config_update_handler_) {
            config_update_handler_(cfg);
        }
    }

    // Send ACK: access_device/v1/cmd/{uuid}/setConfig_reply & access_device/v1/cmd/setConfig_reply
    std::string reply_topic_scoped = cfg.prefix + "/cmd/" + cfg.uuid + "/setConfig_reply";
    std::string reply_topic_global = cfg.prefix + "/cmd/setConfig_reply";
    json ack = {
        {"serialNo", serial_no.empty() ? generate_serial("cfg_ack") : serial_no},
        {"uuid", cfg.uuid},
        {"code", "000000"},
        {"message", "Success"},
        {"time", get_current_epoch_sec()},
        {"data", {}}
    };

    std::cout << "[MQTT-Access] >>> Publishing setConfig_reply to: " << reply_topic_scoped << "\n";
    publish_custom(reply_topic_scoped, ack, 0);
    publish_custom(reply_topic_global, ack, 0);

    if (broker_changed) {
        std::cout << "[MQTT-Access] Broker configuration updated. Reconnecting to new broker...\n";
        // Reconnect on next tick
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Protocol 6: Remote Control (remoteControl & remoteControl_reply)
// ─────────────────────────────────────────────────────────────────────────────

void MqttAccessClient::handle_cmd_remote_control(const std::string& serial_no, const nlohmann::json& payload) {
    MqttAccessConfig cfg = get_config();
    std::string action = "openDoor";
    int door_no = 1;
    int duration_ms = 3000;

    if (payload.contains("data")) {
        const auto& d = payload["data"];
        action      = d.value("action", "openDoor");
        door_no     = d.value("doorNo", 1);
        duration_ms = d.value("durationMs", 3000);
    }

    std::cout << "[MQTT-Access] Executing remoteControl: action=" << action
              << ", door=" << door_no << ", duration=" << duration_ms << "ms\n";

    bool success = true;
    if (remote_control_handler_) {
        success = remote_control_handler_(action, door_no, duration_ms);
    }

    // Send ACK: access_device/v1/cmd/{uuid}/remoteControl_reply & access_device/v1/cmd/remoteControl_reply
    std::string reply_topic_scoped = cfg.prefix + "/cmd/" + cfg.uuid + "/remoteControl_reply";
    std::string reply_topic_global = cfg.prefix + "/cmd/remoteControl_reply";
    json ack = {
        {"serialNo", serial_no.empty() ? generate_serial("ctrl_ack") : serial_no},
        {"uuid", cfg.uuid},
        {"code", success ? "000000" : "100001"},
        {"message", success ? "Success" : "Failed to trigger door relay"},
        {"time", get_current_epoch_sec()},
        {"data", {}}
    };

    publish_custom(reply_topic_scoped, ack, 0);
    publish_custom(reply_topic_global, ack, 0);
}

void MqttAccessClient::handle_event_connect_reply(const nlohmann::json& payload) {
    std::string code = payload.value("code", "");
    std::string msg  = payload.value("message", "");
    std::cout << "[MQTT-Access] Handshake connect_reply acknowledged by platform: code="
              << code << " (" << msg << ")\n";
}

void MqttAccessClient::handle_cmd_ota(const std::string& serial_no, const nlohmann::json& payload) {
    MqttAccessConfig cfg = get_config();
    std::cout << "[MQTT-Access] Received OTA / Firmware Upgrade command from platform.\n";

    std::string version = "";
    std::string url = "";
    bool reboot = false;
    uint64_t size_bytes = 0;

    if (payload.contains("data") && payload["data"].is_object()) {
        const auto& d = payload["data"];
        version    = d.value("version", "");
        url        = d.value("url", d.value("downloadUrl", d.value("presigned_url", "")));
        reboot     = d.value("reboot", false);
        size_bytes = d.value("size", d.value("size_bytes", 0ULL));
    } else {
        version    = payload.value("version", "");
        url        = payload.value("url", payload.value("downloadUrl", payload.value("presigned_url", "")));
        reboot     = payload.value("reboot", false);
        size_bytes = payload.value("size", payload.value("size_bytes", 0ULL));
    }

    if (version.empty()) {
        auto slash = url.rfind('/');
        if (slash != std::string::npos) {
            version = url.substr(slash + 1);
            if (version.ends_with(".zip")) version = version.substr(0, version.length() - 4);
        } else {
            version = "firmware_update";
        }
    }

    std::cout << "[MQTT-Access] OTA target: version=" << version
              << ", url=" << url << ", reboot=" << reboot << "\n";

    bool started = false;
    if (ota_update_handler_) {
        started = ota_update_handler_(version, url, reboot, size_bytes);
    }

    // Send ACK: access_device/v1/cmd/upgradeFirmware_reply & ota_reply
    json ack = {
        {"serialNo", serial_no.empty() ? generate_serial("ota_ack") : serial_no},
        {"uuid", cfg.uuid},
        {"code", started ? "000000" : "100002"},
        {"message", started ? "Firmware upgrade initiated successfully" : "Failed to initiate upgrade or update in progress"},
        {"time", get_current_epoch_sec()},
        {"data", {
            {"version", version},
            {"status", started ? "in_progress" : "failed"}
        }}
    };

    std::string fw_reply_topic = cfg.prefix + "/cmd/upgradeFirmware_reply";
    std::string ota_reply_topic = cfg.prefix + "/cmd/ota_reply";
    std::cout << "[MQTT-Access] >>> Publishing reply to: " << fw_reply_topic << " and " << ota_reply_topic << "\n";
    publish_custom(fw_reply_topic, ack, 1);
    publish_custom(ota_reply_topic, ack, 1);
}

bool MqttAccessClient::publish_ota_status(const std::string& status, const std::string& message, const std::string& version) {
    MqttAccessConfig cfg = get_config();
    std::string topic = cfg.prefix + "/event/ota_status";
    json payload = {
        {"uuid", cfg.uuid},
        {"serialNo", generate_serial("ota_stat")},
        {"time", get_current_epoch_sec()},
        {"data", {
            {"status", status},
            {"message", message},
            {"version", version}
        }}
    };
    return publish_custom(topic, payload, 1);
}

} // namespace app

