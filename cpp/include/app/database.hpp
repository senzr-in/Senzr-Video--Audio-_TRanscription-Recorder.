#pragma once
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <memory>
#include <functional>
#include "app/database_models.hpp"
#include "session_pipeline/models.hpp"

// Forward declaration — avoids including sqlite3.h in every translation unit.
struct sqlite3;
struct sqlite3_stmt;

namespace app {

// ─────────────────────────────────────────────────────────────────────────────
// Database
//
// SDK-standard SQLite wrapper for the Senzr Edge Gateway.
//
// Design principles:
//   1. RAII lifetime — connection opens in constructor, closes in destructor.
//   2. Persistent connection — one sqlite3* shared for the lifetime of the
//      object; never opened/closed per call (avoids per-call overhead).
//   3. Thread safety — a write mutex serialises all INSERT/UPDATE/DELETE;
//      readers under WAL mode can run concurrently.
//   4. Prepared statements — every parameterised query uses sqlite3_bind_*;
//      no string concatenation for data values.
//   5. Scoped transactions — batch writes are wrapped in
//      BEGIN / COMMIT / ROLLBACK via the inner Transaction RAII helper.
//
// Usage:
//   app::Database db;
//   db.open();          // call once at startup (or use Database::open_or_die)
//   db.log_system_event("BOOT");
//   // ... use throughout lifetime ...
//   // destructor closes automatically
// ─────────────────────────────────────────────────────────────────────────────
class Database {
public:
    explicit Database(std::string db_path = "database/edge_gateway.db");
    ~Database();

    // Non-copyable, movable
    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&)                 = default;
    Database& operator=(Database&&)      = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // Opens the database, applies WAL pragmas, creates all tables, and
    // runs startup crash recovery. Call once at application startup.
    // Returns false on failure (error is logged to stderr).
    bool open();
    bool init_db() { return open(); }

    // Returns true if the database connection is open and healthy.
    bool is_open() const noexcept;

    // ── Schema maintenance ────────────────────────────────────────────────────
    // Deletes detection_events rows older than keep_days (default 14).
    bool vacuum_old_events(int keep_days = 14);

    // Deletes device_telemetry rows older than keep_days (default 7).
    bool vacuum_old_telemetry(int keep_days = 7);

    // ── Sessions ──────────────────────────────────────────────────────────────
    // Inserts a new session row when recording begins.
    // session_dir_str: absolute path string of the recording directory.
    bool insert_session(const session_pipeline::SessionInfo& info,
                        const std::string& session_dir_str);

    // Sets end_time and duration_sec when recording stops.
    bool update_session_end(const std::string& session_id,
                            const std::string& end_time,
                            double             duration_sec);

    // Updates upload_status; optionally sets cloud S3 prefix and error text.
    // status: "pending" | "in_progress" | "done" | "failed"
    bool update_session_upload_status(const std::string& session_id,
                                      const std::string& status,
                                      const std::string& s3_prefix = "",
                                      const std::string& error     = "");

    // Returns the last `limit` sessions as a JSON array string.
    std::string get_sessions_json(int limit = 50) const;

    // ── Upload Queue ──────────────────────────────────────────────────────────
    // Writes a durable upload job. file_type: "video"|"audio"|"session_json".
    bool enqueue_upload(const std::string& session_id,
                        const std::string& file_path,
                        const std::string& file_type);

    // Marks a job row as successfully completed.
    bool mark_upload_done(int row_id);
    bool mark_upload_done(const std::string& session_id, const std::string& file_path);

    // Marks a job row as failed, increments attempts, stores error text.
    bool mark_upload_failed(int row_id, const std::string& error);
    bool mark_upload_failed(const std::string& session_id,
                            const std::string& file_path,
                            const std::string& error);

    // Returns every pending or in_flight upload row for crash recovery.
    std::vector<UploadQueueRow> get_pending_upload_jobs() const;

    // ── Detection Events ──────────────────────────────────────────────────────
    // Persists one person or plate detection event.
    bool log_detection_event(const DetectionEventRow& event);

    // Returns the last `limit` detection events as a JSON array string.
    // Optionally filtered by event_type (empty = all).
    std::string get_detection_events_json(int                limit       = 100,
                                          const std::string& type_filter = "") const;

    // ── Device Telemetry ──────────────────────────────────────────────────────
    // Stores a periodic hardware health snapshot.
    bool insert_telemetry(const TelemetryRow& row);

    // Returns the last `limit` rows as a JSON array string.
    std::string get_telemetry_json(int limit = 60) const;

    // ── System Events ─────────────────────────────────────────────────────────
    // Records a system-level audit event.
    // severity: "info" | "warn" | "error"
    bool log_system_event(const std::string& event_type,
                          const std::string& detail   = "",
                          const std::string& severity = "info");

    // Returns the last `limit` rows as a JSON array string.
    std::string get_system_events_json(int limit = 100) const;

    // Returns total count of rows in the specified table.
    int get_row_count(const std::string& table_name) const;

    // ── Key-Value Store (system_info table) ───────────────────────────────────
    bool                       set_kv(const std::string& key,
                                      const std::string& value);
    std::optional<std::string> get_kv(const std::string& key) const;

    // ── Scoped Transaction ────────────────────────────────────────────────────
    // RAII helper for batching multiple writes atomically.
    //
    // Usage:
    //   {
    //       auto tx = db.begin_transaction();
    //       db.log_detection_event(e1);
    //       db.log_detection_event(e2);
    //       tx.commit();   // omit to auto-rollback on scope exit
    //   }
    class Transaction {
    public:
        explicit Transaction(Database& db);
        ~Transaction();                          // rolls back if not committed
        void commit();
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
    private:
        Database& db_;
        bool      committed_{false};
    };

    Transaction begin_transaction() { return Transaction{*this}; }

private:
    // ── Internal helpers ──────────────────────────────────────────────────────

    // Executes a SQL string that takes no parameters (DDL, PRAGMA, etc.).
    // Caller must hold write_mutex_ if needed.
    bool exec_noparams(const char* sql) noexcept;

    // Prepares a statement, executes it with the provided bind callback,
    // then finalises it. The bind callback receives the sqlite3_stmt* and
    // should call sqlite3_bind_* on it; returns SQLITE_OK on success.
    // Acquires write_mutex_ for the duration.
    bool exec_write(const char* sql,
                    const std::function<int(sqlite3_stmt*)>& bind_fn) noexcept;

    // Executes a SELECT query and calls row_fn once per result row.
    // Does NOT acquire write_mutex_ (readers are lock-free under WAL).
    void exec_read(const char* sql,
                   const std::function<int(sqlite3_stmt*)>& bind_fn,
                   const std::function<void(sqlite3_stmt*)>& row_fn) const noexcept;

    std::string   db_path_;
    sqlite3*      db_{nullptr};           // persistent connection (RAII)
    mutable std::recursive_mutex write_mutex_; // serialises writes; re-entrant for open()/vacuum()
};

} // namespace app
