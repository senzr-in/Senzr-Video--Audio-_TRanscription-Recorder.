#include "app/updater.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>

void test_sha256() {
    auto tmp = std::filesystem::temp_directory_path() / "test_sha.txt";
    {
        std::ofstream f(tmp);
        f << "hello world";
    }
    // echo -n "hello world" | sha256sum -> b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
    std::string hash = app::OtaUpdater::sha256_of_file(tmp);
    std::filesystem::remove(tmp);
    assert(hash == "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
    std::cout << "[PASS] test_sha256\n";
}

void test_version_io() {
    auto tmp_dir = std::filesystem::temp_directory_path() / "test_ota_version";
    std::filesystem::create_directories(tmp_dir);

    app::UpdaterConfig cfg;
    cfg.current_dir = tmp_dir;
    app::OtaUpdater updater(cfg);

    assert(updater.get_current_version() == "0.0.0");
    updater.set_current_version("1.2.3");
    assert(updater.get_current_version() == "1.2.3");

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_version_io\n";
}

void test_config_env() {
    auto env_file = std::filesystem::temp_directory_path() / "test_updater.env";
    {
        std::ofstream f(env_file);
        f << "S3_BUCKET=custom-bucket\n";
        f << "MQTT_PORT=1888\n";
        f << "CHECK_INTERVAL=45\n";
    }

    auto cfg = app::UpdaterConfig::load_from_env(env_file);
    std::filesystem::remove(env_file);

    assert(cfg.s3_bucket == "custom-bucket");
    assert(cfg.mqtt_port == 1888);
    assert(cfg.check_interval_sec == 45);
    std::cout << "[PASS] test_config_env\n";
}

void test_concurrency_guard() {
    auto tmp_dir = std::filesystem::temp_directory_path() / "test_ota_concurrency";
    std::filesystem::create_directories(tmp_dir);

    app::UpdaterConfig cfg;
    cfg.current_dir = tmp_dir;
    app::OtaUpdater updater(cfg);

    assert(!updater.is_updating());
    std::cout << "[PASS] test_concurrency_guard\n";
    std::filesystem::remove_all(tmp_dir);
}

int main() {
    std::cout << "=== OtaUpdater Tests ===\n";
    test_sha256();
    test_version_io();
    test_config_env();
    test_concurrency_guard();
    std::cout << "All updater tests PASSED.\n";
    return 0;
}
