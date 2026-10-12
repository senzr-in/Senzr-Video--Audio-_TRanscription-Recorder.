#include "app/ble_access_manager.hpp"
#include "app/database.hpp"
#include "app/gpio_manager.hpp"
#include <iostream>
#include <cassert>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>

static std::string compute_hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    HMAC(EVP_sha256(),
         key.data(), key.size(),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         md, &md_len);

    std::ostringstream oss;
    for (size_t i = 0; i < md_len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)md[i];
    }
    return oss.str();
}

int main() {
    std::cout << "===============================================\n";
    std::cout << "  RUNNING SENZR BLE ACCESS SUBSYSTEM TESTS\n";
    std::cout << "===============================================\n\n";

    auto& ble = app::BleAccessManager::instance();
    app::Database db("database/test_ble.db");
    db.open();
    app::GpioManager gpio;

    assert(ble.start(&db, &gpio));
    std::cout << "[PASS] BleAccessManager started successfully\n";

    // ── Test 1: Challenge Nonce Generation ──────────────────────────────────
    std::string client_mac = "AA:BB:CC:DD:EE:FF";
    std::string nonce = ble.generate_challenge(client_mac);
    assert(!nonce.empty());
    assert(nonce.length() == 32); // 16 bytes in hex = 32 chars
    std::cout << "[PASS] 128-bit Challenge Nonce generated: " << nonce << "\n";

    // ── Test 2: Valid HMAC-SHA256 Response Verification ─────────────────────
    std::string credential_secret = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    std::string valid_token = compute_hmac_sha256(credential_secret, nonce);
    
    bool auth_ok = ble.verify_response(client_mac, valid_token, credential_secret);
    assert(auth_ok);
    std::cout << "[PASS] Valid HMAC Response Token verified successfully\n";

    // ── Test 3: Forged / Invalid HMAC Response Rejection ─────────────────────
    std::string nonce2 = ble.generate_challenge(client_mac);
    std::string invalid_token = compute_hmac_sha256("wrong_secret_key", nonce2);
    bool auth_fail = ble.verify_response(client_mac, invalid_token, credential_secret);
    assert(!auth_fail);
    std::cout << "[PASS] Forged / Tampered HMAC Token rejected as expected\n";

    // ── Test 4: Replay Attack / Nonce Expiration Check ──────────────────────
    std::string nonce3 = ble.generate_challenge(client_mac);
    std::string token3 = compute_hmac_sha256(credential_secret, nonce3);
    
    // First attempt succeeds
    assert(ble.verify_response(client_mac, token3, credential_secret));
    
    // Immediate replay with same token without new challenge should fail
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Regenerate new challenge on gateway side
    ble.generate_challenge(client_mac);
    // Old token3 must now fail against new challenge
    assert(!ble.verify_response(client_mac, token3, credential_secret));
    std::cout << "[PASS] Anti-replay nonce rotation verified\n";

    // ── Test 5: RSSI Distance Smoothing & Actuation Threshold ───────────────
    ble.update_rssi(client_mac, -90);
    ble.update_rssi(client_mac, -85);
    ble.update_rssi(client_mac, -88);
    assert(!ble.is_in_actuation_zone(client_mac, -70)); // Too far for turnstile

    // Approaching gate:
    ble.update_rssi(client_mac, -65);
    ble.update_rssi(client_mac, -62);
    ble.update_rssi(client_mac, -64);
    ble.update_rssi(client_mac, -60);
    assert(ble.is_in_actuation_zone(client_mac, -70)); // In range!
    std::cout << "[PASS] RSSI rolling median filtering & actuation zone verified\n";

    // ── Test 6: Status JSON Output ──────────────────────────────────────────
    auto st = ble.status_json();
    assert(st["running"] == true);
    assert(st.contains("active_clients"));
    std::cout << "[PASS] Status JSON serialization verified\n";

    ble.stop();
    std::cout << "\n===============================================\n";
    std::cout << "  ALL BLE ACCESS SUBSYSTEM TESTS PASSED! \n";
    std::cout << "===============================================\n";
    return 0;
}
