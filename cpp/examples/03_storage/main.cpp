#include <senzr/senzr.hpp>
#include <iostream>

int main() {
    std::cout << "=== Example 03: Offline SQLite Storage & Event Logging ===\n";

    senzr::StorageConfig cfg;
    cfg.db_path = "database/edge_gateway.db";

    senzr::Storage storage(cfg);
    auto open_res = storage.open();
    if (!open_res) {
        std::cerr << "Database Open Error: " << open_res.error().message() << "\n";
        return 1;
    }

    std::cout << "Database opened with WAL mode.\n";

    // Log access event
    senzr::StorageEvent ev;
    ev.session_id = "demo_session_101";
    ev.event_type = "PLATE_MATCH_GRANTED";
    ev.timestamp = 1728384000.0;
    ev.confidence = 0.98f;

    auto log_res = storage.log_event(ev);
    if (log_res) {
        std::cout << "Event logged successfully to SQLite.\n";
    }

    // Retrieve recent events
    auto events_res = storage.get_recent_events(5);
    if (events_res) {
        std::cout << "Retrieved " << events_res.value().size() << " recent events.\n";
    }

    return 0;
}
