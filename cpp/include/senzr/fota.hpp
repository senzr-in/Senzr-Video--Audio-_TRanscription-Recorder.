#pragma once

#include "app/updater.hpp"
#include <string>
#include <functional>
#include <nlohmann/json.hpp>

namespace senzr::fota {

using Config = app::UpdaterConfig;
using ReleaseMetadata = app::ReleaseMetadata;

class Client {
public:
    explicit Client(Config config = Config::load_from_env());
    ~Client() = default;

    // Check S3 for releases
    std::optional<ReleaseMetadata> check_latest();

    // Trigger update
    bool update(const std::string& version, bool reboot_on_success = false,
                const std::string& direct_url = "", uint64_t size_bytes = 0);

    // Rollback to previous version
    bool rollback();

    // Current version & status
    std::string get_current_version() const;
    nlohmann::json get_status_json() const;

    // Run daemon background listener (MQTT + S3 fallback polling)
    void start_daemon();
    void stop_daemon();

    // Direct access to underlying updater engine
    app::OtaUpdater& get_engine() { return engine_; }

private:
    Config config_;
    app::OtaUpdater engine_;
};

} // namespace senzr::fota
