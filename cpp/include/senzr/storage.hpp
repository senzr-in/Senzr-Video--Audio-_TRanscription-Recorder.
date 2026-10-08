#pragma once

#include "senzr/export.hpp"
#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>

namespace senzr {
inline namespace v1 {

/**
 * @brief Configuration settings for the embedded SQLite/SQLCipher offline storage engine.
 */
struct SENZR_API StorageConfig {
    std::string db_path{"database/edge_gateway.db"};
    int busy_timeout_ms{5000};
    bool enable_wal{true};
};

/**
 * @brief Normalized event record stored offline for audit trails and cloud synchronization.
 */
struct SENZR_API StorageEvent {
    std::string event_id;
    std::string session_id;
    std::string event_type;
    double timestamp{0.0};
    std::string payload;
    float confidence{0.0f};
};

/**
 * @brief Resilient upload job entry in the crash-recovery queue.
 */
struct SENZR_API StorageUploadJob {
    int id{0};
    std::string session_id;
    std::string file_path;
    std::string file_type; // "video", "audio", "metadata"
    std::string status;    // "pending", "in_flight", "done", "failed"
    int attempts{0};
    std::string error_message;
};

/**
 * @brief Thread-safe, crash-resilient SQLite storage engine with WAL mode and transaction support.
 */
class SENZR_API Storage {
public:
    explicit Storage(StorageConfig config = {});
    ~Storage();

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage(Storage&&) noexcept;
    Storage& operator=(Storage&&) noexcept;

    // ── Connection Lifecycle ──────────────────────────────────────────────────
    [[nodiscard]] Result<void> open();
    [[nodiscard]] bool is_open() const noexcept;
    void close();

    // ── Event Operations ──────────────────────────────────────────────────────
    [[nodiscard]] Result<void> log_event(const StorageEvent& event);
    [[nodiscard]] Result<std::vector<StorageEvent>> get_recent_events(int limit = 50);

    // ── Durable Upload Queue (Crash-Recovery) ──────────────────────────────────
    [[nodiscard]] Result<void> enqueue_upload(const std::string& session_id,
                                              const std::string& file_path,
                                              const std::string& file_type);
    [[nodiscard]] Result<void> mark_upload_done(int job_id);
    [[nodiscard]] Result<void> mark_upload_failed(int job_id, const std::string& error_msg);

    // ── Maintenance ───────────────────────────────────────────────────────────
    [[nodiscard]] Result<void> vacuum_old_records(int keep_days = 14);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace v1
} // namespace senzr
