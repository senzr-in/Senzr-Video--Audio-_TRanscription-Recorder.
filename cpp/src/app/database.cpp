#include "app/database.hpp"
#include <filesystem>
#include <sqlite3.h>

namespace app {

Database::Database(std::string db_path) : db_path_(std::move(db_path)) {}

void Database::init_db() const {
    std::filesystem::create_directories(std::filesystem::path(db_path_).parent_path());
    sqlite3* db = nullptr;
    if (sqlite3_open(db_path_.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return;
    }
    const char* system_info_sql = R"sql(
        CREATE TABLE IF NOT EXISTS system_info (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            key TEXT UNIQUE NOT NULL,
            value TEXT NOT NULL
        );
    )sql";
    const char* config_changes_sql = R"sql(
        CREATE TABLE IF NOT EXISTS config_changes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            client_ip TEXT,
            client_mac TEXT,
            user_agent TEXT,
            changed_fields TEXT,
            old_config TEXT,
            new_config TEXT
        );
    )sql";
    sqlite3_exec(db, system_info_sql, nullptr, nullptr, nullptr);
    sqlite3_exec(db, config_changes_sql, nullptr, nullptr, nullptr);
    sqlite3_close(db);
}

}
