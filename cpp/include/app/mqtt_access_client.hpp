#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <nlohmann/json.hpp>

struct mosquitto;

namespace app {

struct MqttAccessConfig {
    std::string broker_host{"mqtt.fieldseasy.com"};
    int         broker_port{1883};
    std::string username{"iot-device"};
    std::string password{"Senzr123"};
    std::string prefix{"access_device/v1"};
    std::string uuid;                      // Device UUID (e.g. DEVICE_UUID_105053189)
    std::string device_name{"Entrance Controller"};
    std::string device_model{"CC104"};
    std::string app_version{"1.0.6"};
    std::string sys_version{"1.0.6"};
    int         heartbeat_interval_sec{30};
    bool        enabled{true};
};

class MqttAccessClient {
public:
    explicit MqttAccessClient(MqttAccessConfig config = {});
    ~MqttAccessClient();

    // Lifecycle
    bool start();
    void stop();
    bool is_connected() const noexcept { return is_connected_.load(); }

    // Configuration
    MqttAccessConfig get_config() const;
    void update_config(const MqttAccessConfig& config);

    // Uplink Publishing Methods
    bool publish_connect_handshake();
    bool publish_heartbeat(const nlohmann::json& telemetry_data);
    bool publish_access_record(const std::string& record_type,
                               const std::string& identifier,
                               float confidence,
                               const std::string& snapshot_url = "",
                               bool relay_triggered = false);
    bool publish_custom(const std::string& topic, const nlohmann::json& payload, int qos = 1);

    // Callback Registration for Downlink Commands
    using RemoteControlHandler = std::function<bool(const std::string& action, int door_no, int duration_ms)>;
    void set_remote_control_handler(RemoteControlHandler handler);

    using ConfigUpdateHandler = std::function<void(const MqttAccessConfig& new_cfg)>;
    void set_config_update_handler(ConfigUpdateHandler handler);

    // FOTA OTA update handler
    using OtaUpdateHandler = std::function<bool(const std::string& version,
                                                const std::string& url,
                                                bool reboot,
                                                uint64_t size_bytes)>;
    void set_ota_update_handler(OtaUpdateHandler handler);
    bool publish_ota_status(const std::string& status, const std::string& message, const std::string& version);

    // Callbacks for mosquitto C library
    void on_connect(int rc);
    void on_disconnect(int rc);
    void on_message(const std::string& topic, const std::string& payload);

private:
    void init_device_uuid_if_empty();
    std::string generate_serial(const std::string& prefix = "ser") const;
    uint64_t get_current_epoch_sec() const;
    std::string get_lan_ip() const;

    void handle_cmd_set_config(const std::string& serial_no, const nlohmann::json& payload);
    void handle_cmd_remote_control(const std::string& serial_no, const nlohmann::json& payload);
    void handle_cmd_ota(const std::string& serial_no, const nlohmann::json& payload);
    void handle_event_connect_reply(const nlohmann::json& payload);

    mutable std::mutex       config_mutex_;
    MqttAccessConfig         config_;

    struct mosquitto*        mosq_{nullptr};
    std::atomic<bool>        is_connected_{false};
    std::atomic<bool>        running_{false};
    std::jthread             heartbeat_thread_;

    RemoteControlHandler     remote_control_handler_;
    ConfigUpdateHandler      config_update_handler_;
    OtaUpdateHandler         ota_update_handler_;
};

} // namespace app
