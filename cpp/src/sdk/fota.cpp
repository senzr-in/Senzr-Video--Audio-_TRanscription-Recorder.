#include "senzr/fota.hpp"
#include <filesystem>
#include <iostream>

namespace senzr::fota {

Client::Client(Config config)
    : config_(std::move(config))
    , engine_(config_)
{
}

std::optional<ReleaseMetadata> Client::check_latest() {
    return engine_.fetch_latest_metadata();
}

bool Client::update(const std::string& version, bool reboot_on_success,
                    const std::string& direct_url, uint64_t size_bytes) {
    return engine_.run_update(version, reboot_on_success, direct_url, size_bytes);
}

bool Client::rollback() {
    try {
        if (!std::filesystem::exists(config_.backup_dir)) {
            std::cerr << "[senzr::fota] No backup directory exists at " << config_.backup_dir << '\n';
            return false;
        }

        app::OtaUpdater::service_action("stop", config_.app_service);
        std::filesystem::remove_all(config_.current_dir);
        std::filesystem::copy(config_.backup_dir, config_.current_dir,
                              std::filesystem::copy_options::recursive);
        app::OtaUpdater::service_action("start", config_.app_service);

        engine_.publish_status("rollback", "Manual rollback to previous backup successful");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[senzr::fota] Rollback failed: " << e.what() << '\n';
        return false;
    }
}

std::string Client::get_current_version() const {
    return engine_.get_current_version();
}

nlohmann::json Client::get_status_json() const {
    return {
        {"device_id", config_.device_id},
        {"mac", config_.device_id},
        {"current_version", get_current_version()},
        {"s3_bucket", config_.s3_bucket},
        {"mqtt_broker", config_.mqtt_broker},
        {"mqtt_topic", config_.mqtt_topic},
        {"reboot_on_success", config_.reboot_on_success}
    };
}

void Client::start_daemon() {
    engine_.run();
}

void Client::stop_daemon() {
    engine_.stop();
}

} // namespace senzr::fota
