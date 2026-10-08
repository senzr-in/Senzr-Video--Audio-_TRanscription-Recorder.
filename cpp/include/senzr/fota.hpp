#pragma once

#include "senzr/export.hpp"
#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <string>
#include <optional>
#include <memory>
#include <functional>

namespace senzr {
inline namespace v1 {
namespace fota {

/**
 * @brief Information about a released firmware package.
 */
struct SENZR_API ReleaseMetadata {
    std::string version;
    std::string file_name;
    std::string checksum_sha256;
    std::string download_url;
    uint64_t size_bytes{0};
};

using ReleaseInfo = ReleaseMetadata;

/**
 * @brief Current operational state of the FOTA update engine.
 */
struct SENZR_API FotaStatus {
    std::string current_version;
    std::string target_version;
    std::string state; // "idle", "checking", "downloading", "verifying", "installing", "testing", "success", "failed", "rollback"
    std::string last_error;
    int progress_percent{0};
    bool is_updating{false};
};

/**
 * @brief Configuration parameters for FOTA updates.
 */
struct SENZR_API FotaConfig {
    std::string device_id;
    std::string download_dir{"/opt/edge-gateway/downloads"};
    std::string backup_dir{"/opt/edge-gateway/backup"};
    std::string current_dir{"/opt/edge-gateway/current"};
    int check_interval_sec{60};
    bool reboot_on_success{true};
};

using Config = FotaConfig;

/**
 * @brief Client for managing Over-The-Air (FOTA) updates and A/B partition rollbacks.
 */
class SENZR_API Client {
public:
    explicit Client(FotaConfig config = {});
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;

    // ── Update Lifecycle ──────────────────────────────────────────────────────
    [[nodiscard]] Result<std::optional<ReleaseMetadata>> check_latest();
    [[nodiscard]] Result<void> update(const std::string& version,
                                      bool reboot_on_success = true,
                                      const std::string& direct_url = "",
                                      uint64_t size_bytes = 0);
    [[nodiscard]] Result<void> rollback();

    // ── Inspection & State ────────────────────────────────────────────────────
    [[nodiscard]] std::string get_current_version() const;
    [[nodiscard]] FotaStatus get_status() const;
    [[nodiscard]] bool is_updating() const;

    // ── Daemon Mode (Periodic polling & remote triggers) ──────────────────────
    void start_daemon();
    void stop_daemon();

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace fota
} // namespace v1
} // namespace senzr
