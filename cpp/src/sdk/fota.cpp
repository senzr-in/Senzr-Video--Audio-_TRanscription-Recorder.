#include "senzr/fota.hpp"
#include "app/updater.hpp"

namespace senzr::fota {

struct Client::Impl {
    app::UpdaterConfig app_cfg;
    app::OtaUpdater updater;

    explicit Impl(FotaConfig cfg)
        : app_cfg(app::UpdaterConfig::load_from_env()),
          updater(app_cfg) {
        if (!cfg.device_id.empty()) app_cfg.device_id = cfg.device_id;
        if (!cfg.download_dir.empty()) app_cfg.downloads_dir = cfg.download_dir;
        if (!cfg.backup_dir.empty()) app_cfg.backup_dir = cfg.backup_dir;
        if (!cfg.current_dir.empty()) app_cfg.current_dir = cfg.current_dir;
        app_cfg.reboot_on_success = cfg.reboot_on_success;
    }
};

Client::Client(FotaConfig config)
    : pimpl_(std::make_unique<Impl>(std::move(config))) {}

Client::~Client() = default;
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;

Result<std::optional<ReleaseMetadata>> Client::check_latest() {
    auto meta = pimpl_->updater.fetch_latest_metadata();
    if (!meta) {
        return std::optional<ReleaseMetadata>(std::nullopt);
    }
    ReleaseMetadata res;
    res.version = meta->version;
    res.file_name = meta->file;
    res.checksum_sha256 = meta->checksum;
    res.download_url = meta->download_url;
    res.size_bytes = meta->size_bytes;
    return std::optional<ReleaseMetadata>(res);
}

Result<void> Client::update(const std::string& version,
                            bool reboot_on_success,
                            const std::string& direct_url,
                            uint64_t size_bytes,
                            const std::string& signature,
                            bool allow_downgrade) {
    if (pimpl_->updater.run_update(version, reboot_on_success, direct_url, size_bytes, "", signature, allow_downgrade)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::UpdateFailed, "OTA Update failed for version " + version);
}

Result<void> Client::rollback() {
    if (pimpl_->updater.manual_rollback()) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::InternalError, "Rollback failed or no valid backup snapshot exists");
}

std::string Client::get_current_version() const {
    return pimpl_->updater.get_current_version();
}

FotaStatus Client::get_status() const {
    FotaStatus st;
    st.current_version = get_current_version();
    st.is_updating = is_updating();
    st.state = st.is_updating ? "updating" : "idle";
    return st;
}

bool Client::is_updating() const {
    return pimpl_->updater.is_updating();
}

void Client::start_daemon() {
    pimpl_->updater.run();
}

void Client::stop_daemon() {
    pimpl_->updater.stop();
}

} // namespace senzr::fota
