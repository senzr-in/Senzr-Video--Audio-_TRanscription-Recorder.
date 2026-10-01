// test_config.cpp — ConfigManager unit tests
#include "app/config_manager.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>

using namespace app;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::cerr << "FAIL: " << msg << '\n'; return false; } } while(0)

bool test_roundtrip() {
    const std::string path = "/tmp/test_edge_config.json";
    ConfigManager mgr(path);

    ConfigModel original;
    original.devicename          = "test-device";
    original.mode                = "object";
    original.wifissid            = "MyWifi";
    original.wifipassword        = "secret123";
    original.provisioningenabled = true;
    original.cameraconnected     = false;

    mgr.write_config(original);

    ConfigModel loaded = mgr.read_config();
    CHECK(loaded.devicename          == original.devicename,          "devicename");
    CHECK(loaded.mode                == original.mode,                "mode");
    CHECK(loaded.wifissid            == original.wifissid,            "wifissid");
    CHECK(loaded.wifipassword        == original.wifipassword,        "wifipassword");
    CHECK(loaded.provisioningenabled == original.provisioningenabled, "provisioningenabled");
    CHECK(loaded.cameraconnected     == original.cameraconnected,     "cameraconnected");

    std::filesystem::remove(path);
    std::cout << "[PASS] test_roundtrip\n";
    return true;
}

bool test_missing_file_returns_default() {
    ConfigManager mgr("/tmp/nonexistent_xyzabc.json");
    ConfigModel cfg = mgr.read_config();
    CHECK(cfg.devicename.empty(), "default devicename is empty");
    std::cout << "[PASS] test_missing_file_returns_default\n";
    return true;
}

int main() {
    std::cout << "=== ConfigManager Tests ===\n";
    int failed = 0;
    failed += !test_roundtrip();
    failed += !test_missing_file_returns_default();
    std::cout << (failed == 0 ? "\nAll tests PASSED.\n" : "\nSome tests FAILED.\n");
    return failed;
}
