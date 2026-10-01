// test_session.cpp — Session layer unit tests
#include "session_pipeline/session.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <regex>

using namespace session_pipeline;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::cerr << "FAIL: " << msg << '\n'; return false; } } while(0)

bool test_create_session_format() {
    auto tmp = std::filesystem::temp_directory_path() / "test_sessions";
    auto h = create_session(tmp);

    // ID format: session_YYYYMMDD_HHMMSS
    std::regex pattern(R"(session_\d{8}_\d{6})");
    CHECK(std::regex_match(h.session_id, pattern), "session_id format");
    CHECK(std::filesystem::is_directory(h.session_dir), "session dir created");

    std::filesystem::remove_all(tmp);
    std::cout << "[PASS] test_create_session_format\n";
    return true;
}

bool test_write_read_session_json() {
    auto tmp = std::filesystem::temp_directory_path() / "test_sessions2";
    auto h   = create_session(tmp);

    SessionInfo info;
    info.session_id   = h.session_id;
    info.session_dir  = h.session_dir;
    info.start_time   = "2024-01-01T00:00:00Z";
    info.end_time     = "2024-01-01T00:01:00Z";
    info.device_name  = "RK3588 Platform";  // generic; runtime: read /proc/device-tree/model
    info.model_name   = "yolov8.rknn";

    write_session_json(h.session_dir, info);

    auto loaded = read_session_json(h.session_dir);
    CHECK(loaded.session_id   == info.session_id,  "session_id");
    CHECK(loaded.start_time   == info.start_time,  "start_time");
    CHECK(loaded.end_time     == info.end_time,     "end_time");
    CHECK(loaded.device_name  == info.device_name,  "device_name");

    std::filesystem::remove_all(tmp);
    std::cout << "[PASS] test_write_read_session_json\n";
    return true;
}

bool test_update_transcript_status() {
    auto tmp = std::filesystem::temp_directory_path() / "test_sessions3";
    auto h   = create_session(tmp);

    SessionInfo info;
    info.session_id  = h.session_id;
    info.session_dir = h.session_dir;
    info.start_time  = "2024-01-01T00:00:00Z";
    write_session_json(h.session_dir, info);

    auto json_path = h.session_dir / "session.json";
    update_session_transcript_status(json_path, "completed");

    auto loaded = read_session_json(h.session_dir);
    CHECK(loaded.transcript_status == "completed", "transcript_status updated");

    std::filesystem::remove_all(tmp);
    std::cout << "[PASS] test_update_transcript_status\n";
    return true;
}

int main() {
    std::cout << "=== Session Tests ===\n";
    int failed = 0;
    failed += !test_create_session_format();
    failed += !test_write_read_session_json();
    failed += !test_update_transcript_status();
    std::cout << (failed == 0 ? "\nAll tests PASSED.\n" : "\nSome tests FAILED.\n");
    return failed;
}
