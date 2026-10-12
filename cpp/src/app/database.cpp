#include "app/database.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <functional>

namespace app {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

struct StatementGuard {
    sqlite3_stmt* stmt{nullptr};
    ~StatementGuard() {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
    }
    sqlite3_stmt* get() const noexcept { return stmt; }
    sqlite3_stmt** ptr() noexcept { return &stmt; }
    operator sqlite3_stmt*() const noexcept { return stmt; }
};

std::string escape_json_string(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                      << static_cast<int>(static_cast<unsigned char>(c));
                } else {
                    o << c;
                }
                break;
        }
    }
    return o.str();
}

std::string text_from_col(sqlite3_stmt* stmt, int col) {
    const unsigned char* txt = sqlite3_column_text(stmt, col);
    return txt ? reinterpret_cast<const char*>(txt) : std::string{};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Database Constructor & Destructor
// ─────────────────────────────────────────────────────────────────────────────

Database::Database(std::string db_path)
    : db_path_(std::move(db_path)) {}

Database::~Database() {
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

bool Database::is_open() const noexcept {
    return db_ != nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal execution primitives
// ─────────────────────────────────────────────────────────────────────────────

bool Database::exec_noparams(const char* sql) noexcept {
    if (!db_) {
        std::cerr << "[Database] exec_noparams called with null connection\n";
        return false;
    }
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "[Database] exec_noparams failed (" << rc << "): "
                  << (err_msg ? err_msg : "unknown") << "\nSQL: " << sql << "\n";
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        return false;
    }
    return true;
}

bool Database::exec_write(const char* sql,
                          const std::function<int(sqlite3_stmt*)>& bind_fn) noexcept {
    if (!db_) {
        std::cerr << "[Database] exec_write called with null db_\n";
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(write_mutex_);

    StatementGuard stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, stmt.ptr(), nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[Database] prepare failed (" << rc << "): "
                  << sqlite3_errmsg(db_) << "\nSQL: " << sql << "\n";
        return false;
    }

    if (bind_fn) {
        rc = bind_fn(stmt.get());
        if (rc != SQLITE_OK) {
            std::cerr << "[Database] bind failed (" << rc << "): "
                      << sqlite3_errmsg(db_) << "\n";
            return false;
        }
    }

    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::cerr << "[Database] step failed (" << rc << "): "
                  << sqlite3_errmsg(db_) << "\n";
        return false;
    }
    return true;
}

void Database::exec_read(const char* sql,
                         const std::function<int(sqlite3_stmt*)>& bind_fn,
                         const std::function<void(sqlite3_stmt*)>& row_fn) const noexcept {
    if (!db_) {
        std::cerr << "[Database] exec_read called with null db_\n";
        return;
    }

    StatementGuard stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, stmt.ptr(), nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[Database] read prepare failed (" << rc << "): "
                  << sqlite3_errmsg(db_) << "\nSQL: " << sql << "\n";
        return;
    }

    if (bind_fn) {
        rc = bind_fn(stmt.get());
        if (rc != SQLITE_OK) {
            std::cerr << "[Database] read bind failed (" << rc << "): "
                      << sqlite3_errmsg(db_) << "\n";
            return;
        }
    }

    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        if (row_fn) {
            row_fn(stmt.get());
        }
    }

    if (rc != SQLITE_DONE) {
        std::cerr << "[Database] read step ended unexpectedly (" << rc << "): "
                  << sqlite3_errmsg(db_) << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Transaction RAII Implementation
// ─────────────────────────────────────────────────────────────────────────────

Database::Transaction::Transaction(Database& db)
    : db_(db) {
    db_.exec_noparams("BEGIN IMMEDIATE;");
}

Database::Transaction::~Transaction() {
    if (!committed_) {
        db_.exec_noparams("ROLLBACK;");
    }
}

void Database::Transaction::commit() {
    if (!committed_) {
        db_.exec_noparams("COMMIT;");
        committed_ = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle: open & schema initialization
// ─────────────────────────────────────────────────────────────────────────────

bool Database::open() {
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);

    if (db_) {
        return true; // Already open
    }

    std::error_code ec;
    std::filesystem::path p(db_path_);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    int rc = sqlite3_open_v2(
        db_path_.c_str(),
        &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);

    if (rc != SQLITE_OK || !db_) {
        std::cerr << "[Database] Failed to open SQLite database at '"
                  << db_path_ << "': "
                  << (db_ ? sqlite3_errmsg(db_) : "allocation failure") << "\n";
        if (db_) {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
        return false;
    }

    // If encryption key is set (SQLCipher 256-bit AES), execute PRAGMA key
    if (!encryption_key_.empty()) {
        std::string pragma_key = "PRAGMA key = '" + encryption_key_ + "';";
        exec_noparams(pragma_key.c_str());
    }

    // Set busy timeout (wait up to 5000ms if locked)
    sqlite3_busy_timeout(db_, 5000);

    // Apply high-performance, crash-safe embedded WAL pragmas
    const char* pragmas =
        "PRAGMA journal_mode = WAL;"
        "PRAGMA synchronous  = NORMAL;"
        "PRAGMA cache_size   = -4096;"
        "PRAGMA foreign_keys = ON;"
        "PRAGMA temp_store   = MEMORY;";

    if (!exec_noparams(pragmas)) {
        std::cerr << "[Database] Warning: failed to apply standard pragmas\n";
    }

    // DDL schema statements executed individually with standard SQLite defaults
    const std::vector<const char*> statements = {
        // 1. Sessions table
        "CREATE TABLE IF NOT EXISTS sessions ("
        "    id                INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id        TEXT    UNIQUE NOT NULL,"
        "    device_name       TEXT,"
        "    model_name        TEXT    DEFAULT 'yolov8.rknn',"
        "    start_time        TEXT    NOT NULL,"
        "    end_time          TEXT,"
        "    session_dir       TEXT    NOT NULL,"
        "    video_path        TEXT,"
        "    audio_path        TEXT,"
        "    session_json_path TEXT,"
        "    transcript_path   TEXT,"
        "    duration_sec      REAL,"
        "    upload_status     TEXT    NOT NULL DEFAULT 'pending',"
        "    upload_attempts   INTEGER NOT NULL DEFAULT 0,"
        "    last_upload_at    TEXT,"
        "    cloud_s3_prefix   TEXT,"
        "    error_msg         TEXT,"
        "    created_at        TEXT    NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");",
        "CREATE INDEX IF NOT EXISTS idx_sessions_upload ON sessions(upload_status);",
        "CREATE INDEX IF NOT EXISTS idx_sessions_time   ON sessions(start_time DESC);",

        // 2. Upload queue table
        "CREATE TABLE IF NOT EXISTS upload_queue ("
        "    id              INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id      TEXT    NOT NULL,"
        "    file_path       TEXT    NOT NULL,"
        "    file_type       TEXT    NOT NULL,"
        "    s3_key          TEXT,"
        "    status          TEXT    NOT NULL DEFAULT 'pending',"
        "    attempts        INTEGER NOT NULL DEFAULT 0,"
        "    last_attempt_at TEXT,"
        "    error_msg       TEXT,"
        "    enqueued_at     TEXT    NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");",
        "CREATE INDEX IF NOT EXISTS idx_upload_status  ON upload_queue(status);",
        "CREATE INDEX IF NOT EXISTS idx_upload_session ON upload_queue(session_id);",

        // 3. Detection events table
        "CREATE TABLE IF NOT EXISTS detection_events ("
        "    id              INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id      TEXT,"
        "    event_type      TEXT    NOT NULL,"
        "    timestamp       REAL    NOT NULL,"
        "    frame_number    INTEGER,"
        "    confidence      REAL,"
        "    vision_mode     TEXT,"
        "    bbox_x          INTEGER,"
        "    bbox_y          INTEGER,"
        "    bbox_w          INTEGER,"
        "    bbox_h          INTEGER,"
        "    plate_raw       TEXT,"
        "    plate_clean     TEXT,"
        "    plate_formatted TEXT,"
        "    plate_valid     INTEGER,"
        "    plate_multiline INTEGER,"
        "    plate_x         INTEGER,"
        "    plate_y         INTEGER,"
        "    created_at      TEXT    NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");",
        "CREATE INDEX IF NOT EXISTS idx_det_session ON detection_events(session_id);",
        "CREATE INDEX IF NOT EXISTS idx_det_type    ON detection_events(event_type);",
        "CREATE INDEX IF NOT EXISTS idx_det_ts      ON detection_events(timestamp DESC);",

        // 4. Device telemetry table
        "CREATE TABLE IF NOT EXISTS device_telemetry ("
        "    id               INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    timestamp        TEXT    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "    cpu_temp_c       REAL,"
        "    npu_temp_c       REAL,"
        "    gpu_temp_c       REAL,"
        "    ram_used_mb      INTEGER,"
        "    ram_total_mb     INTEGER,"
        "    disk_used_gb     REAL,"
        "    disk_total_gb    REAL,"
        "    uptime_sec       INTEGER,"
        "    camera_active    INTEGER,"
        "    recording_active INTEGER,"
        "    fps_average      REAL,"
        "    detection_count  INTEGER,"
        "    vision_mode      TEXT"
        ");",

        // 5. System events table
        "CREATE TABLE IF NOT EXISTS system_events ("
        "    id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    timestamp   TEXT    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "    event_type  TEXT    NOT NULL,"
        "    detail      TEXT,"
        "    severity    TEXT    NOT NULL DEFAULT 'info'"
        ");",
        "CREATE INDEX IF NOT EXISTS idx_sysevt_type ON system_events(event_type);",
        "CREATE INDEX IF NOT EXISTS idx_sysevt_ts   ON system_events(timestamp DESC);",

        // 6. Key-Value metadata store
        "CREATE TABLE IF NOT EXISTS system_info ("
        "    key         TEXT PRIMARY KEY NOT NULL,"
        "    value       TEXT,"
        "    updated_at  TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");",

        // 7. Audit log
        "CREATE TABLE IF NOT EXISTS config_changes ("
        "    id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    timestamp   TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "    key         TEXT NOT NULL,"
        "    old_value   TEXT,"
        "    new_value   TEXT,"
        "    changed_by  TEXT DEFAULT 'system'"
        ");",

        // 8. Offline Access Credentials (Module 4)
        "CREATE TABLE IF NOT EXISTS access_credentials ("
        "    id              INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    credential_hash TEXT UNIQUE NOT NULL,"
        "    holder_name     TEXT,"
        "    role            TEXT NOT NULL DEFAULT 'resident',"
        "    plate_number    TEXT,"
        "    valid_from      INTEGER NOT NULL DEFAULT 0,"
        "    valid_until     INTEGER NOT NULL DEFAULT 0,"
        "    is_revoked      INTEGER NOT NULL DEFAULT 0,"
        "    created_at      TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");",
        "CREATE INDEX IF NOT EXISTS idx_cred_hash  ON access_credentials(credential_hash);",
        "CREATE INDEX IF NOT EXISTS idx_cred_plate ON access_credentials(plate_number);"
    };

    bool schema_ok = true;
    for (const char* stmt : statements) {
        if (!exec_noparams(stmt)) {
            std::cerr << "[Database] Error initializing statement: " << stmt << "\n";
            schema_ok = false;
        }
    }
    if (!schema_ok) {
        std::cerr << "[Database] Warning: some schema statements failed\n";
    }


    // Startup Crash Recovery: Reset stuck in_flight uploads to pending
    const char* recovery_sql =
        "UPDATE upload_queue SET status = 'pending' WHERE status = 'in_flight';";
    exec_noparams(recovery_sql);

    // Prune stale records at boot
    vacuum_old_events(14);
    vacuum_old_telemetry(7);

    // Seed default physical human gate master credential
    AccessCredentialRow master_cred;
    master_cred.credential_hash = "senzr_sec_k992_prod_human_gate_master";
    master_cred.holder_name = "Master Human Gate Operator";
    master_cred.role = "admin";
    add_access_credential(master_cred);

    // Log boot event
    log_system_event("BOOT", "Edge gateway database initialized successfully", "info");
    std::cout << "[Database] Database initialized and WAL ready at: " << db_path_ << std::endl;

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Schema Maintenance
// ─────────────────────────────────────────────────────────────────────────────

bool Database::vacuum_old_events(int keep_days) {
    const char* sql =
        "DELETE FROM detection_events "
        "WHERE timestamp < (strftime('%s', 'now') - ? * 86400);";
    return exec_write(sql, [keep_days](sqlite3_stmt* stmt) {
        return sqlite3_bind_int(stmt, 1, keep_days);
    });
}

bool Database::vacuum_old_telemetry(int keep_days) {
    const char* sql =
        "DELETE FROM device_telemetry "
        "WHERE timestamp < datetime('now', '-' || ? || ' days');";
    return exec_write(sql, [keep_days](sqlite3_stmt* stmt) {
        return sqlite3_bind_int(stmt, 1, keep_days);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Sessions
// ─────────────────────────────────────────────────────────────────────────────

bool Database::insert_session(const session_pipeline::SessionInfo& info,
                              const std::string& session_dir_str) {
    const char* sql =
        "INSERT INTO sessions ("
        "    session_id, device_name, model_name, start_time, session_dir,"
        "    video_path, audio_path, session_json_path, transcript_path, upload_status"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    std::filesystem::path dir(session_dir_str);
    std::string vid_path = (dir / info.video_file).string();
    std::string aud_path = (dir / info.audio_file).string();
    std::string json_path = (dir / "session.json").string();
    std::string txt_path = (dir / info.transcript_file).string();

    return exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, info.session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, info.device_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, info.model_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, info.start_time.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, session_dir_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, vid_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, aud_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, json_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, txt_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 10, info.upload_status.c_str(), -1, SQLITE_TRANSIENT);
        return SQLITE_OK;
    });
}

bool Database::update_session_end(const std::string& session_id,
                                  const std::string& end_time,
                                  double duration_sec) {
    const char* sql =
        "UPDATE sessions SET end_time = ?, duration_sec = ? WHERE session_id = ?;";
    return exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, end_time.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, duration_sec);
        sqlite3_bind_text(stmt, 3, session_id.c_str(), -1, SQLITE_TRANSIENT);
        return SQLITE_OK;
    });
}

bool Database::update_session_upload_status(const std::string& session_id,
                                            const std::string& status,
                                            const std::string& s3_prefix,
                                            const std::string& error) {
    const char* sql =
        "UPDATE sessions SET "
        "    upload_status   = ?,"
        "    last_upload_at  = strftime('%Y-%m-%dT%H:%M:%S', 'now'),"
        "    cloud_s3_prefix = CASE WHEN ? != '' THEN ? ELSE cloud_s3_prefix END,"
        "    error_msg       = ?,"
        "    upload_attempts = upload_attempts + 1 "
        "WHERE session_id = ?;";

    return exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, s3_prefix.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, s3_prefix.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, error.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, session_id.c_str(), -1, SQLITE_TRANSIENT);
        return SQLITE_OK;
    });
}

std::string Database::get_sessions_json(int limit) const {
    const char* sql =
        "SELECT id, session_id, device_name, model_name, start_time, end_time, "
        "       session_dir, video_path, audio_path, duration_sec, "
        "       upload_status, upload_attempts, last_upload_at, cloud_s3_prefix, error_msg, created_at "
        "FROM sessions ORDER BY id DESC LIMIT ?;";

    std::ostringstream out;
    out << "[";
    bool first = true;

    exec_read(
        sql,
        [limit](sqlite3_stmt* stmt) {
            return sqlite3_bind_int(stmt, 1, limit > 0 ? limit : 50);
        },
        [&](sqlite3_stmt* stmt) {
            if (!first) out << ",";
            first = false;

            out << "{"
                << "\"id\":"                << sqlite3_column_int(stmt, 0) << ","
                << "\"session_id\":\""      << escape_json_string(text_from_col(stmt, 1)) << "\","
                << "\"device_name\":\""     << escape_json_string(text_from_col(stmt, 2)) << "\","
                << "\"model_name\":\""      << escape_json_string(text_from_col(stmt, 3)) << "\","
                << "\"start_time\":\""      << escape_json_string(text_from_col(stmt, 4)) << "\","
                << "\"end_time\":\""        << escape_json_string(text_from_col(stmt, 5)) << "\","
                << "\"session_dir\":\""     << escape_json_string(text_from_col(stmt, 6)) << "\","
                << "\"video_path\":\""      << escape_json_string(text_from_col(stmt, 7)) << "\","
                << "\"audio_path\":\""      << escape_json_string(text_from_col(stmt, 8)) << "\","
                << "\"duration_sec\":"      << sqlite3_column_double(stmt, 9) << ","
                << "\"upload_status\":\""   << escape_json_string(text_from_col(stmt, 10)) << "\","
                << "\"upload_attempts\":"   << sqlite3_column_int(stmt, 11) << ","
                << "\"last_upload_at\":\""  << escape_json_string(text_from_col(stmt, 12)) << "\","
                << "\"cloud_s3_prefix\":\"" << escape_json_string(text_from_col(stmt, 13)) << "\","
                << "\"error_msg\":\""       << escape_json_string(text_from_col(stmt, 14)) << "\","
                << "\"created_at\":\""      << escape_json_string(text_from_col(stmt, 15)) << "\""
                << "}";
        });

    out << "]";
    return out.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Upload Queue
// ─────────────────────────────────────────────────────────────────────────────

bool Database::enqueue_upload(const std::string& session_id,
                              const std::string& file_path,
                              const std::string& file_type) {
    const char* sql =
        "INSERT INTO upload_queue ("
        "    session_id, file_path, file_type, s3_key, status"
        ") VALUES (?, ?, ?, ?, 'pending');";

    std::filesystem::path fp(file_path);
    std::string s3_key = "sessions/" + session_id + "/" + fp.filename().string();

    return exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, file_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, file_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, s3_key.c_str(), -1, SQLITE_TRANSIENT);
        return SQLITE_OK;
    });
}

bool Database::mark_upload_done(int row_id) {
    const char* sql =
        "UPDATE upload_queue SET "
        "    status          = 'done',"
        "    last_attempt_at = strftime('%Y-%m-%dT%H:%M:%S', 'now'),"
        "    error_msg       = NULL "
        "WHERE id = ?;";

    return exec_write(sql, [row_id](sqlite3_stmt* stmt) {
        return sqlite3_bind_int(stmt, 1, row_id);
    });
}

bool Database::mark_upload_done(const std::string& session_id, const std::string& file_path) {
    const char* sql =
        "UPDATE upload_queue SET "
        "    status          = 'done',"
        "    last_attempt_at = strftime('%Y-%m-%dT%H:%M:%S', 'now'),"
        "    error_msg       = NULL "
        "WHERE session_id = ? AND file_path = ?;";

    bool ok = exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, file_path.c_str(), -1, SQLITE_TRANSIENT);
        return SQLITE_OK;
    });

    if (ok) {
        // Check if all upload items for this session are complete
        const char* count_sql =
            "SELECT COUNT(*) FROM upload_queue "
            "WHERE session_id = ? AND status != 'done';";
        int pending_count = 0;
        exec_read(
            count_sql,
            [&](sqlite3_stmt* stmt) {
                return sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
            },
            [&](sqlite3_stmt* stmt) {
                pending_count = sqlite3_column_int(stmt, 0);
            });
        if (pending_count == 0) {
            update_session_upload_status(session_id, "done");
        }
    }
    return ok;
}

bool Database::mark_upload_failed(int row_id, const std::string& error) {
    const char* sql =
        "UPDATE upload_queue SET "
        "    status          = 'failed',"
        "    attempts        = attempts + 1,"
        "    last_attempt_at = strftime('%Y-%m-%dT%H:%M:%S', 'now'),"
        "    error_msg       = ? "
        "WHERE id = ?;";

    return exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, error.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, row_id);
        return SQLITE_OK;
    });
}

bool Database::mark_upload_failed(const std::string& session_id,
                                  const std::string& file_path,
                                  const std::string& error) {
    const char* sql =
        "UPDATE upload_queue SET "
        "    status          = 'failed',"
        "    attempts        = attempts + 1,"
        "    last_attempt_at = strftime('%Y-%m-%dT%H:%M:%S', 'now'),"
        "    error_msg       = ? "
        "WHERE session_id = ? AND file_path = ?;";

    bool ok = exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, error.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, file_path.c_str(), -1, SQLITE_TRANSIENT);
        return SQLITE_OK;
    });

    if (ok) {
        update_session_upload_status(session_id, "failed", "", error);
    }
    return ok;
}

std::vector<UploadQueueRow> Database::get_pending_upload_jobs() const {
    const char* sql =
        "SELECT id, session_id, file_path, file_type, s3_key, status, attempts "
        "FROM upload_queue "
        "WHERE status IN ('pending', 'in_flight') "
        "ORDER BY id ASC;";

    std::vector<UploadQueueRow> jobs;
    exec_read(
        sql,
        nullptr,
        [&](sqlite3_stmt* stmt) {
            UploadQueueRow r;
            r.id         = sqlite3_column_int(stmt, 0);
            r.session_id = text_from_col(stmt, 1);
            r.file_path  = text_from_col(stmt, 2);
            r.file_type  = text_from_col(stmt, 3);
            r.s3_key     = text_from_col(stmt, 4);
            r.status     = text_from_col(stmt, 5);
            r.attempts   = sqlite3_column_int(stmt, 6);
            jobs.push_back(std::move(r));
        });

    return jobs;
}

// ─────────────────────────────────────────────────────────────────────────────
// Detection Events
// ─────────────────────────────────────────────────────────────────────────────

bool Database::log_detection_event(const DetectionEventRow& event) {
    const char* sql =
        "INSERT INTO detection_events ("
        "    session_id, event_type, timestamp, frame_number, confidence, vision_mode,"
        "    bbox_x, bbox_y, bbox_w, bbox_h,"
        "    plate_raw, plate_clean, plate_formatted, plate_valid, plate_multiline, plate_x, plate_y"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    return exec_write(sql, [&](sqlite3_stmt* stmt) {
        if (!event.session_id.empty()) {
            sqlite3_bind_text(stmt, 1, event.session_id.c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 1);
        }

        sqlite3_bind_text(stmt, 2, event.event_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, event.timestamp);
        sqlite3_bind_int(stmt, 4, event.frame_number);
        sqlite3_bind_double(stmt, 5, static_cast<double>(event.confidence));
        sqlite3_bind_text(stmt, 6, event.vision_mode.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_bind_int(stmt, 7, event.bbox_x);
        sqlite3_bind_int(stmt, 8, event.bbox_y);
        sqlite3_bind_int(stmt, 9, event.bbox_w);
        sqlite3_bind_int(stmt, 10, event.bbox_h);

        sqlite3_bind_text(stmt, 11, event.plate_raw.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 12, event.plate_clean.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 13, event.plate_formatted.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 14, event.plate_valid ? 1 : 0);
        sqlite3_bind_int(stmt, 15, event.plate_multiline ? 1 : 0);
        sqlite3_bind_int(stmt, 16, event.plate_x);
        sqlite3_bind_int(stmt, 17, event.plate_y);

        return SQLITE_OK;
    });
}

std::string Database::get_detection_events_json(int limit,
                                                const std::string& type_filter) const {
    bool has_filter = !type_filter.empty();
    const char* sql_all =
        "SELECT id, session_id, event_type, timestamp, frame_number, confidence, "
        "       vision_mode, bbox_x, bbox_y, bbox_w, bbox_h, "
        "       plate_raw, plate_clean, plate_formatted, plate_valid, plate_multiline, plate_x, plate_y, created_at "
        "FROM detection_events ORDER BY id DESC LIMIT ?;";

    const char* sql_filter =
        "SELECT id, session_id, event_type, timestamp, frame_number, confidence, "
        "       vision_mode, bbox_x, bbox_y, bbox_w, bbox_h, "
        "       plate_raw, plate_clean, plate_formatted, plate_valid, plate_multiline, plate_x, plate_y, created_at "
        "FROM detection_events WHERE event_type = ? ORDER BY id DESC LIMIT ?;";

    std::ostringstream out;
    out << "[";
    bool first = true;

    auto bind_cb = [&](sqlite3_stmt* stmt) {
        if (has_filter) {
            sqlite3_bind_text(stmt, 1, type_filter.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, limit > 0 ? limit : 100);
        } else {
            sqlite3_bind_int(stmt, 1, limit > 0 ? limit : 100);
        }
        return SQLITE_OK;
    };

    auto row_cb = [&](sqlite3_stmt* stmt) {
        if (!first) out << ",";
        first = false;

        out << "{"
            << "\"id\":"              << sqlite3_column_int(stmt, 0) << ","
            << "\"session_id\":\""    << escape_json_string(text_from_col(stmt, 1)) << "\","
            << "\"event_type\":\""    << escape_json_string(text_from_col(stmt, 2)) << "\","
            << "\"timestamp\":"       << sqlite3_column_double(stmt, 3) << ","
            << "\"frame_number\":"    << sqlite3_column_int(stmt, 4) << ","
            << "\"confidence\":"      << sqlite3_column_double(stmt, 5) << ","
            << "\"vision_mode\":\""   << escape_json_string(text_from_col(stmt, 6)) << "\","
            << "\"bbox\":["           << sqlite3_column_int(stmt, 7) << ","
                                      << sqlite3_column_int(stmt, 8) << ","
                                      << sqlite3_column_int(stmt, 9) << ","
                                      << sqlite3_column_int(stmt, 10) << "],"
            << "\"plate_raw\":\""     << escape_json_string(text_from_col(stmt, 11)) << "\","
            << "\"plate_clean\":\""   << escape_json_string(text_from_col(stmt, 12)) << "\","
            << "\"plate_formatted\":\""<< escape_json_string(text_from_col(stmt, 13)) << "\","
            << "\"plate_valid\":"     << (sqlite3_column_int(stmt, 14) ? "true" : "false") << ","
            << "\"plate_multiline\":" << (sqlite3_column_int(stmt, 15) ? "true" : "false") << ","
            << "\"plate_pos\":["      << sqlite3_column_int(stmt, 16) << ","
                                      << sqlite3_column_int(stmt, 17) << "],"
            << "\"created_at\":\""    << escape_json_string(text_from_col(stmt, 18)) << "\""
            << "}";
    };

    exec_read(has_filter ? sql_filter : sql_all, bind_cb, row_cb);

    out << "]";
    return out.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Device Telemetry
// ─────────────────────────────────────────────────────────────────────────────

bool Database::insert_telemetry(const TelemetryRow& row) {
    const char* sql =
        "INSERT INTO device_telemetry ("
        "    cpu_temp_c, npu_temp_c, gpu_temp_c,"
        "    ram_used_mb, ram_total_mb, disk_used_gb, disk_total_gb,"
        "    uptime_sec, camera_active, recording_active, fps_average, detection_count, vision_mode"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    return exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_double(stmt, 1, static_cast<double>(row.cpu_temp_c));
        sqlite3_bind_double(stmt, 2, static_cast<double>(row.npu_temp_c));
        sqlite3_bind_double(stmt, 3, static_cast<double>(row.gpu_temp_c));
        sqlite3_bind_int(stmt, 4, row.ram_used_mb);
        sqlite3_bind_int(stmt, 5, row.ram_total_mb);
        sqlite3_bind_double(stmt, 6, static_cast<double>(row.disk_used_gb));
        sqlite3_bind_double(stmt, 7, static_cast<double>(row.disk_total_gb));
        sqlite3_bind_int(stmt, 8, row.uptime_sec);
        sqlite3_bind_int(stmt, 9, row.camera_active ? 1 : 0);
        sqlite3_bind_int(stmt, 10, row.recording_active ? 1 : 0);
        sqlite3_bind_double(stmt, 11, static_cast<double>(row.fps_average));
        sqlite3_bind_int(stmt, 12, row.detection_count);
        sqlite3_bind_text(stmt, 13, row.vision_mode.c_str(), -1, SQLITE_TRANSIENT);
        return SQLITE_OK;
    });
}

std::string Database::get_telemetry_json(int limit) const {
    const char* sql =
        "SELECT timestamp, cpu_temp_c, npu_temp_c, gpu_temp_c, "
        "       ram_used_mb, ram_total_mb, disk_used_gb, disk_total_gb, uptime_sec, "
        "       camera_active, recording_active, fps_average, detection_count, vision_mode "
        "FROM device_telemetry ORDER BY id DESC LIMIT ?;";

    std::ostringstream out;
    out << "[";
    bool first = true;

    exec_read(
        sql,
        [limit](sqlite3_stmt* stmt) {
            return sqlite3_bind_int(stmt, 1, limit > 0 ? limit : 60);
        },
        [&](sqlite3_stmt* stmt) {
            if (!first) out << ",";
            first = false;

            out << "{"
                << "\"timestamp\":\""        << escape_json_string(text_from_col(stmt, 0)) << "\","
                << "\"cpu_temp_c\":"         << sqlite3_column_double(stmt, 1) << ","
                << "\"npu_temp_c\":"         << sqlite3_column_double(stmt, 2) << ","
                << "\"gpu_temp_c\":"         << sqlite3_column_double(stmt, 3) << ","
                << "\"ram_used_mb\":"        << sqlite3_column_int(stmt, 4) << ","
                << "\"ram_total_mb\":"       << sqlite3_column_int(stmt, 5) << ","
                << "\"disk_used_gb\":"       << sqlite3_column_double(stmt, 6) << ","
                << "\"disk_total_gb\":"      << sqlite3_column_double(stmt, 7) << ","
                << "\"uptime_sec\":"         << sqlite3_column_int(stmt, 8) << ","
                << "\"camera_active\":"      << (sqlite3_column_int(stmt, 9) ? "true" : "false") << ","
                << "\"recording_active\":"   << (sqlite3_column_int(stmt, 10) ? "true" : "false") << ","
                << "\"fps_average\":"        << sqlite3_column_double(stmt, 11) << ","
                << "\"detection_count\":"    << sqlite3_column_int(stmt, 12) << ","
                << "\"vision_mode\":\""      << escape_json_string(text_from_col(stmt, 13)) << "\""
                << "}";
        });

    out << "]";
    return out.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// System Events
// ─────────────────────────────────────────────────────────────────────────────

bool Database::log_system_event(const std::string& event_type,
                                const std::string& detail,
                                const std::string& severity) {
    const char* sql =
        "INSERT INTO system_events (event_type, detail, severity) VALUES (?, ?, ?);";

    return exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, event_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, detail.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, severity.c_str(), -1, SQLITE_TRANSIENT);
        return SQLITE_OK;
    });
}

std::string Database::get_system_events_json(int limit) const {
    const char* sql =
        "SELECT id, timestamp, event_type, detail, severity "
        "FROM system_events ORDER BY id DESC LIMIT ?;";

    std::ostringstream out;
    out << "[";
    bool first = true;

    exec_read(
        sql,
        [limit](sqlite3_stmt* stmt) {
            return sqlite3_bind_int(stmt, 1, limit > 0 ? limit : 100);
        },
        [&](sqlite3_stmt* stmt) {
            if (!first) out << ",";
            first = false;

            out << "{"
                << "\"id\":"          << sqlite3_column_int(stmt, 0) << ","
                << "\"timestamp\":\"" << escape_json_string(text_from_col(stmt, 1)) << "\","
                << "\"event_type\":\""<< escape_json_string(text_from_col(stmt, 2)) << "\","
                << "\"detail\":\""    << escape_json_string(text_from_col(stmt, 3)) << "\","
                << "\"severity\":\""  << escape_json_string(text_from_col(stmt, 4)) << "\""
                << "}";
        });

    out << "]";
    return out.str();
}

int Database::get_row_count(const std::string& table_name) const {
    std::string actual_table = (table_name == "recording_sessions") ? "sessions" : table_name;
    if (actual_table != "sessions" &&
        actual_table != "upload_queue" &&
        actual_table != "detection_events" &&
        actual_table != "device_telemetry" &&
        actual_table != "system_events" &&
        actual_table != "access_credentials") {
        return 0;
    }
    std::string sql = "SELECT COUNT(*) FROM " + actual_table + ";";
    int count = 0;
    exec_read(sql.c_str(), [](sqlite3_stmt*) { return SQLITE_OK; }, [&](sqlite3_stmt* stmt) {
        count = sqlite3_column_int(stmt, 0);
    });
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Key-Value Store (system_info table)
// ─────────────────────────────────────────────────────────────────────────────

bool Database::set_kv(const std::string& key, const std::string& value) {
    const char* sql =
        "INSERT INTO system_info (key, value, updated_at) "
        "VALUES (?, ?, strftime('%Y-%m-%dT%H:%M:%S', 'now')) "
        "ON CONFLICT(key) DO UPDATE SET "
        "    value = excluded.value, "
        "    updated_at = excluded.updated_at;";

    return exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
        return SQLITE_OK;
    });
}

std::optional<std::string> Database::get_kv(const std::string& key) const {
    const char* sql = "SELECT value FROM system_info WHERE key = ? LIMIT 1;";
    std::optional<std::string> result;

    exec_read(
        sql,
        [&](sqlite3_stmt* stmt) {
            return sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        },
        [&](sqlite3_stmt* stmt) {
            result = text_from_col(stmt, 0);
        });

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Offline Access Credentials (Module 4)
// ─────────────────────────────────────────────────────────────────────────────

bool Database::add_access_credential(const AccessCredentialRow& cred) {
    const char* sql =
        "INSERT INTO access_credentials ("
        "    credential_hash, holder_name, role, plate_number, valid_from, valid_until, is_revoked"
        ") VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(credential_hash) DO UPDATE SET "
        "    holder_name = excluded.holder_name, "
        "    role = excluded.role, "
        "    plate_number = excluded.plate_number, "
        "    valid_from = excluded.valid_from, "
        "    valid_until = excluded.valid_until, "
        "    is_revoked = excluded.is_revoked;";

    return exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, cred.credential_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, cred.holder_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, cred.role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, cred.plate_number.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 5, cred.valid_from);
        sqlite3_bind_int64(stmt, 6, cred.valid_until);
        sqlite3_bind_int(stmt, 7, cred.is_revoked ? 1 : 0);
        return SQLITE_OK;
    });
}

std::optional<AccessCredentialRow> Database::validate_credential_hash(const std::string& hash) const {
    const char* sql =
        "SELECT id, credential_hash, holder_name, role, plate_number, valid_from, valid_until, is_revoked "
        "FROM access_credentials "
        "WHERE credential_hash = ? AND is_revoked = 0 "
        "LIMIT 1;";

    std::optional<AccessCredentialRow> result;
    exec_read(
        sql,
        [&](sqlite3_stmt* stmt) {
            return sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
        },
        [&](sqlite3_stmt* stmt) {
            AccessCredentialRow r;
            r.id              = sqlite3_column_int(stmt, 0);
            r.credential_hash = text_from_col(stmt, 1);
            r.holder_name     = text_from_col(stmt, 2);
            r.role            = text_from_col(stmt, 3);
            r.plate_number    = text_from_col(stmt, 4);
            r.valid_from      = sqlite3_column_int64(stmt, 5);
            r.valid_until     = sqlite3_column_int64(stmt, 6);
            r.is_revoked      = sqlite3_column_int(stmt, 7) != 0;

            int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            // Validate expiration window if set
            if ((r.valid_from > 0 && now_ts < r.valid_from) ||
                (r.valid_until > 0 && now_ts > r.valid_until)) {
                return; // Expired / Not yet valid
            }
            result = r;
        });

    return result;
}

std::optional<AccessCredentialRow> Database::validate_license_plate(const std::string& plate) const {
    const char* sql =
        "SELECT id, credential_hash, holder_name, role, plate_number, valid_from, valid_until, is_revoked "
        "FROM access_credentials "
        "WHERE plate_number = ? AND is_revoked = 0 "
        "LIMIT 1;";

    std::optional<AccessCredentialRow> result;
    exec_read(
        sql,
        [&](sqlite3_stmt* stmt) {
            return sqlite3_bind_text(stmt, 1, plate.c_str(), -1, SQLITE_TRANSIENT);
        },
        [&](sqlite3_stmt* stmt) {
            AccessCredentialRow r;
            r.id              = sqlite3_column_int(stmt, 0);
            r.credential_hash = text_from_col(stmt, 1);
            r.holder_name     = text_from_col(stmt, 2);
            r.role            = text_from_col(stmt, 3);
            r.plate_number    = text_from_col(stmt, 4);
            r.valid_from      = sqlite3_column_int64(stmt, 5);
            r.valid_until     = sqlite3_column_int64(stmt, 6);
            r.is_revoked      = sqlite3_column_int(stmt, 7) != 0;

            int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            if ((r.valid_from > 0 && now_ts < r.valid_from) ||
                (r.valid_until > 0 && now_ts > r.valid_until)) {
                return;
            }
            result = r;
        });

    return result;
}

bool Database::revoke_credential(const std::string& hash) {
    const char* sql = "UPDATE access_credentials SET is_revoked = 1 WHERE credential_hash = ?;";
    return exec_write(sql, [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
        return SQLITE_OK;
    });
}

std::string Database::get_access_credentials_json(int limit) const {
    std::string sql =
        "SELECT id, credential_hash, holder_name, role, plate_number, valid_from, valid_until, is_revoked "
        "FROM access_credentials ORDER BY id DESC LIMIT " + std::to_string(limit) + ";";

    std::ostringstream out;
    out << "[";
    bool first = true;

    exec_read(
        sql.c_str(),
        [](sqlite3_stmt*) { return SQLITE_OK; },
        [&](sqlite3_stmt* stmt) {
            if (!first) out << ",";
            first = false;
            out << "{"
                << "\"id\":" << sqlite3_column_int(stmt, 0) << ","
                << "\"credential_hash\":\"" << escape_json_string(text_from_col(stmt, 1)) << "\","
                << "\"holder_name\":\"" << escape_json_string(text_from_col(stmt, 2)) << "\","
                << "\"role\":\"" << escape_json_string(text_from_col(stmt, 3)) << "\","
                << "\"plate_number\":\"" << escape_json_string(text_from_col(stmt, 4)) << "\","
                << "\"valid_from\":" << sqlite3_column_int64(stmt, 5) << ","
                << "\"valid_until\":" << sqlite3_column_int64(stmt, 6) << ","
                << "\"is_revoked\":" << (sqlite3_column_int(stmt, 7) ? "true" : "false")
                << "}";
        });

    out << "]";
    return out.str();
}

} // namespace app
