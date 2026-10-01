#pragma once

#include <string>
#include <filesystem>
#include <vector>
#include <optional>
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>

namespace app {

struct ReleaseMetadata {
    std::string version;
    std::string file;
    std::string checksum;
    std::string download_url{""};
    uint64_t size_bytes{0};
};

struct UpdaterConfig {
    std::string device_id{""};
    std::string s3_bucket{"senzr-fota-storage"};
    std::string s3_prefix{"app-releases"};
    std::string s3_region{"us-east-1"};
    std::string mqtt_broker{"mqtt.fieldseasy.com"};
    int mqtt_port{1883};
    std::string mqtt_topic{"edge-gateway/update"};
    std::filesystem::path current_dir{"/opt/edge-gateway/current"};
    std::filesystem::path backup_dir{"/opt/edge-gateway/backup"};
    std::filesystem::path downloads_dir{"/opt/edge-gateway/downloads"};
    int check_interval_sec{60};
    std::string app_service{"edge-gateway"};
    std::string health_check_url{"http://127.0.0.1:8000/api/status"};
    std::filesystem::path lock_file{"/tmp/edge-gateway-update.lock"};
    bool reboot_on_success{false};
    std::vector<std::string> required_files{
        "frontend/index.html",
        "database/config.json"
    };

    static UpdaterConfig load_from_env(const std::filesystem::path& env_file = "/opt/edge-gateway/updater_config.env");
};

class OtaUpdater {
public:
    explicit OtaUpdater(UpdaterConfig config = UpdaterConfig::load_from_env());
    ~OtaUpdater();

    // Core OTA update cycle (supports optional post-update reboot and pre-signed direct URL)
    bool run_update(const std::string& version, bool request_reboot = false,
                    const std::string& direct_url = "", uint64_t size_bytes = 0);

    // Safe async trigger: prevents duplicate threads, manages worker lifecycle
    bool trigger_async_update(const std::string& version, bool request_reboot = false,
                              const std::string& direct_url = "", uint64_t size_bytes = 0);
    bool is_updating() const { return is_updating_.load(); }

    // Periodic checking (S3 poll fallback)
    void check_for_update();
    void start_periodic_check();

    // MQTT listener & reporting
    bool start_mqtt();
    void stop_mqtt();
    bool publish_mqtt(const std::string& topic, const std::string& payload) const;
    void publish_status(const std::string& status, const std::string& message = "", const std::string& target_version = "") const;
    void publish_telemetry() const;

    // Lifecycle
    void run();
    void stop();

    // Helpers exposed for testing
    const UpdaterConfig& get_config() const { return config_; }
    std::string get_current_version() const;
    void set_current_version(const std::string& version) const;
    static std::string get_default_device_id();
    static std::string sha256_of_file(const std::filesystem::path& path);
    static bool service_action(const std::string& action, const std::string& service);
    bool check_disk_space(uint64_t required_bytes) const;
    bool health_check(int retries = 5, int delay_sec = 3) const;
    std::optional<ReleaseMetadata> fetch_latest_metadata() const;
    bool download_release(const std::string& file_name, const std::filesystem::path& dest,
                          const std::string& direct_url = "") const;

private:
    bool acquire_lock() const;
    void release_lock() const;
    void log(const std::string& level, const std::string& msg) const;

    UpdaterConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> is_updating_{false};
    mutable std::mutex log_mutex_;
    mutable std::mutex mqtt_mutex_;
    std::mutex worker_mutex_;
    std::unique_ptr<std::thread> update_worker_;
    std::optional<std::jthread> periodic_thread_;
    void* mosq_{nullptr};  // mosquitto client handle
};

} // namespace app
