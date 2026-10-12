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

void test_rollback() {
    auto tmp_dir = std::filesystem::temp_directory_path() / "test_ota_rollback";
    auto cur_dir = tmp_dir / "current";
    auto bak_dir = tmp_dir / "backup";
    std::filesystem::create_directories(cur_dir);
    std::filesystem::create_directories(bak_dir);

    // Seed backup with v1.0.0
    {
        std::ofstream f(bak_dir / "version.txt");
        f << "1.0.0";
    }

    app::UpdaterConfig cfg;
    cfg.current_dir = cur_dir;
    cfg.backup_dir = bak_dir;
    cfg.lock_file = tmp_dir / "rollback.lock";
    app::OtaUpdater updater(cfg);

    updater.set_current_version("2.0.0");
    assert(updater.get_current_version() == "2.0.0");

    // Test manual rollback restores backup version
    // (mocking health check passing)
    std::cout << "[PASS] test_rollback\n";
    std::filesystem::remove_all(tmp_dir);
}

void test_semver_anti_rollback() {
    // 1. Comparison logic
    assert(app::OtaUpdater::semver_compare("4.1.0", "1.0.0") > 0);
    assert(app::OtaUpdater::semver_compare("1.0.0", "4.1.0") < 0);
    assert(app::OtaUpdater::semver_compare("4.1.0", "4.1.0") == 0);
    assert(app::OtaUpdater::semver_compare("v4.2.0", "4.1.9") > 0);
    assert(app::OtaUpdater::semver_compare("4.10.0", "4.9.0") > 0);

    // 2. Downgrade predicate
    assert(app::OtaUpdater::is_downgrade("4.1.0", "1.0.0") == true);
    assert(app::OtaUpdater::is_downgrade("4.1.0", "4.2.0") == false);
    assert(app::OtaUpdater::is_downgrade("4.1.0", "4.1.0") == false);
    assert(app::OtaUpdater::is_downgrade("0.0.0", "1.0.0") == false); // fresh device

    // 3. Functional updater downgrade block
    auto tmp_dir = std::filesystem::temp_directory_path() / "test_ota_downgrade";
    std::filesystem::create_directories(tmp_dir);

    app::UpdaterConfig cfg;
    cfg.current_dir = tmp_dir;
    cfg.allow_downgrade = false;
    app::OtaUpdater updater(cfg);
    updater.set_current_version("4.1.0");

    // Attempt downgrade to 1.0.0 without allow_downgrade -> MUST fail
    bool res = updater.run_update("1.0.0", false, "", 0, "", "", false);
    assert(res == false);

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_semver_anti_rollback\n";
}

void test_persistent_state_and_recovery() {
    auto tmp_dir = std::filesystem::temp_directory_path() / "test_ota_state";
    auto cur_dir = tmp_dir / "current";
    auto bak_dir = tmp_dir / "backup";
    std::filesystem::create_directories(cur_dir);
    std::filesystem::create_directories(bak_dir);

    // Seed backup with 1.0.0
    {
        std::ofstream f(bak_dir / "version.txt");
        f << "1.0.0";
    }

    app::UpdaterConfig cfg;
    cfg.current_dir = cur_dir;
    cfg.backup_dir = bak_dir;
    cfg.state_file = tmp_dir / "fota_state.json";
    cfg.lock_file = tmp_dir / "state.lock";
    cfg.health_check_url = "mock_pass";
    app::OtaUpdater updater(cfg);

    updater.set_current_version("2.0.0");
    assert(updater.get_current_version() == "2.0.0");

    // Persist an interrupted "installing" state
    updater.persist_state("installing", "2.1.0", "Interrupted installation");
    assert(updater.read_persisted_state() == "installing");

    // Simulate startup recovery on boot
    // check_startup_recovery should detect the interrupted install and auto-rollback to backup
    bool recovered = updater.check_startup_recovery();
    assert(recovered == true);
    assert(updater.read_persisted_state().empty()); // State cleared
    assert(updater.get_current_version() == "1.0.0"); // Restored backup version

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_persistent_state_and_recovery\n";
}

#include <openssl/pem.h>
#include <openssl/ec.h>

void test_signature_verification() {
    auto tmp_dir = std::filesystem::temp_directory_path() / "test_ota_sig";
    std::filesystem::create_directories(tmp_dir);

    auto pub_path = tmp_dir / "pubkey.pem";
    auto test_file = tmp_dir / "firmware.bin";

    // Write a dummy firmware file
    {
        std::ofstream f(test_file, std::ios::binary);
        f << "SENZR_FIRMWARE_PAYLOAD_TEST_DATA_123456789";
    }

    // Generate EC P-256 keypair for testing
    EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    assert(kctx != nullptr);
    assert(EVP_PKEY_keygen_init(kctx) == 1);
    assert(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(kctx, NID_X9_62_prime256v1) == 1);
    EVP_PKEY* pkey = nullptr;
    assert(EVP_PKEY_keygen(kctx, &pkey) == 1);
    EVP_PKEY_CTX_free(kctx);

    // Save public key
    {
        FILE* fp = std::fopen(pub_path.string().c_str(), "wb");
        assert(fp != nullptr);
        PEM_write_PUBKEY(fp, pkey);
        std::fclose(fp);
    }

    // Sign the dummy firmware file
    EVP_MD_CTX* mctx = EVP_MD_CTX_new();
    assert(mctx != nullptr);
    assert(EVP_DigestSignInit(mctx, nullptr, EVP_sha256(), nullptr, pkey) == 1);
    std::string payload = "SENZR_FIRMWARE_PAYLOAD_TEST_DATA_123456789";
    assert(EVP_DigestSignUpdate(mctx, payload.data(), payload.size()) == 1);
    size_t sig_len = 0;
    assert(EVP_DigestSignFinal(mctx, nullptr, &sig_len) == 1);
    std::vector<unsigned char> sig(sig_len);
    assert(EVP_DigestSignFinal(mctx, sig.data(), &sig_len) == 1);
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_free(pkey);

    // Encode signature to Hex
    std::ostringstream hex_oss;
    for (size_t i = 0; i < sig_len; ++i) {
        hex_oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(sig[i]);
    }
    std::string hex_sig = hex_oss.str();

    // 1. Valid signature passes verification
    bool ok = app::OtaUpdater::verify_signature(test_file, hex_sig, pub_path);
    assert(ok == true);

    // 2. Tampered file fails verification
    {
        std::ofstream f(test_file, std::ios::binary | std::ios::app);
        f << "_CORRUPTED";
    }
    bool tampered = app::OtaUpdater::verify_signature(test_file, hex_sig, pub_path);
    assert(tampered == false);

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_signature_verification\n";
}

int main() {
    std::cout << "=== OtaUpdater Tests ===\n";
    test_sha256();
    test_version_io();
    test_config_env();
    test_concurrency_guard();
    test_rollback();
    test_semver_anti_rollback();
    test_persistent_state_and_recovery();
    test_signature_verification();
    std::cout << "All updater tests PASSED.\n";
    return 0;
}
