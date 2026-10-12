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
    std::string app_service{"senzr-edge"};
    std::string health_check_url{"http://127.0.0.1:8000/api/status"};
    std::filesystem::path lock_file{"/tmp/edge-gateway-update.lock"};
    bool reboot_on_success{true};
    std::filesystem::path state_file{"/opt/edge-gateway/fota_state.json"};
    std::filesystem::path public_key_file{"/etc/senzr/certs/fota_public_key.pem"};
    bool allow_downgrade{false};
    bool enforce_signature{false};
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

    // Core OTA update cycle (supports optional post-update reboot, direct URL, explicit checksum, digital signature, and downgrade policy)
    bool run_update(const std::string& version, bool request_reboot = false,
                    const std::string& direct_url = "", uint64_t size_bytes = 0,
                    const std::string& expected_checksum = "",
                    const std::string& signature = "",
                    bool allow_downgrade = false);

    // Apply directly uploaded local firmware package archive (.zip / .tar.gz)
    bool apply_archive_update(const std::filesystem::path& archive_path,
                              const std::string& target_version = "",
                              bool request_reboot = false);

    // Manual rollback to previous backup snapshot
    bool manual_rollback();

    // Safe async trigger: prevents duplicate threads, manages worker lifecycle
    bool trigger_async_update(const std::string& version, bool request_reboot = false,
                              const std::string& direct_url = "", uint64_t size_bytes = 0,
                              const std::string& expected_checksum = "",
                              const std::string& signature = "",
                              bool allow_downgrade = false);
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

    // Helpers exposed for testing & state tracking
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

    // Cryptographic validation and anti-rollback
    static int semver_compare(const std::string& v1, const std::string& v2);
    static bool is_downgrade(const std::string& current_ver, const std::string& target_ver);
    static bool verify_signature(const std::filesystem::path& path,
                                 const std::string& signature_base64_or_hex,
                                 const std::filesystem::path& pubkey_path);

    // Persistent transaction state & crash recovery
    void persist_state(const std::string& state, const std::string& target_version,
                       const std::string& message = "") const;
    void clear_state() const;
    std::string read_persisted_state() const;
    bool check_startup_recovery();

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
