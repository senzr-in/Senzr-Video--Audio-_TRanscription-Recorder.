#include "senzr/storage.hpp"
#include "app/database.hpp"
#include "app/database_models.hpp"
#include <nlohmann/json.hpp>

namespace senzr {
inline namespace v1 {

struct Storage::Impl {
    StorageConfig config;
    app::Database db;

    explicit Impl(StorageConfig cfg)
        : config(std::move(cfg)), db(config.db_path) {}
};

Storage::Storage(StorageConfig config)
    : pimpl_(std::make_unique<Impl>(std::move(config))) {}

Storage::~Storage() = default;
Storage::Storage(Storage&&) noexcept = default;
Storage& Storage::operator=(Storage&&) noexcept = default;

Result<void> Storage::open() {
    if (pimpl_->db.open()) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::StorageError, "Failed to open SQLite database at " + pimpl_->config.db_path);
}

bool Storage::is_open() const noexcept {
    return pimpl_ && pimpl_->db.is_open();
}

void Storage::close() {
    // Database destructor automatically handles closing connection
}

Result<void> Storage::log_event(const StorageEvent& event) {
    if (!is_open()) {
        return Result<void>(ErrorCode::NotInitialized, "Database connection is not open");
    }
    app::DetectionEventRow row;
    row.session_id = event.session_id;
    row.event_type = event.event_type;
    row.timestamp = event.timestamp;
    row.confidence = event.confidence;
    
    if (pimpl_->db.log_detection_event(row)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::StorageError, "Failed to insert event into database");
}

Result<std::vector<StorageEvent>> Storage::get_recent_events(int limit) {
    if (!is_open()) {
        return Result<std::vector<StorageEvent>>(ErrorCode::NotInitialized, "Database connection is not open");
    }
    std::string json_str = pimpl_->db.get_detection_events_json(limit);
    std::vector<StorageEvent> res;
    try {
        auto j = nlohmann::json::parse(json_str);
        if (j.is_array()) {
            for (const auto& item : j) {
                StorageEvent ev;
                ev.session_id = item.value("session_id", "");
                ev.event_type = item.value("event_type", "");
                ev.timestamp  = item.value("timestamp", 0.0);
                ev.confidence = item.value("confidence", 0.0f);
                res.push_back(ev);
            }
        }
    } catch (...) {}
    return res;
}

Result<void> Storage::enqueue_upload(const std::string& session_id,
                                    const std::string& file_path,
                                    const std::string& file_type) {
    if (!is_open()) {
        return Result<void>(ErrorCode::NotInitialized, "Database connection is not open");
    }
    if (pimpl_->db.enqueue_upload(session_id, file_path, file_type)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::StorageError, "Failed to enqueue upload job");
}

Result<void> Storage::mark_upload_done(int job_id) {
    if (!is_open()) {
        return Result<void>(ErrorCode::NotInitialized, "Database connection is not open");
    }
    if (pimpl_->db.mark_upload_done(job_id)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::StorageError, "Failed to mark upload done for job " + std::to_string(job_id));
}

Result<void> Storage::mark_upload_failed(int job_id, const std::string& error_msg) {
    if (!is_open()) {
        return Result<void>(ErrorCode::NotInitialized, "Database connection is not open");
    }
    if (pimpl_->db.mark_upload_failed(job_id, error_msg)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::StorageError, "Failed to mark upload failed for job " + std::to_string(job_id));
}

Result<void> Storage::vacuum_old_records(int keep_days) {
    if (!is_open()) {
        return Result<void>(ErrorCode::NotInitialized, "Database connection is not open");
    }
    bool ok1 = pimpl_->db.vacuum_old_events(keep_days);
    bool ok2 = pimpl_->db.vacuum_old_telemetry(keep_days);
    if (ok1 && ok2) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::StorageError, "Failed to vacuum old database records");
}

} // namespace v1
} // namespace senzr
