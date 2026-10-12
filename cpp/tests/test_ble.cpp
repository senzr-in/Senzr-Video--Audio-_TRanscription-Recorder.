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

    // ── Test 2: Valid HMAC-SHA256 Response Verification (Master Key) ────────
    std::string master_secret = "senzr_sec_k992_prod_human_gate_master";
    std::string valid_token = compute_hmac_sha256(master_secret, nonce);
    
    bool auth_ok = ble.verify_response(client_mac, valid_token, master_secret);
    assert(auth_ok);
    std::cout << "[PASS] Provisioned Secret Key HMAC Response Token verified successfully\n";

    // ── Test 3: Authenticated JSON Payload Support ──────────────────────────
    std::string nonce_json = ble.generate_challenge(client_mac);
    std::string token_json = compute_hmac_sha256(master_secret, nonce_json);
    std::string json_payload = "{\"credential_hash\":\"" + master_secret + "\",\"response\":\"" + token_json + "\"}";
    assert(ble.verify_response(client_mac, json_payload, master_secret));
    std::cout << "[PASS] Authenticated JSON Payload verified on Response / Unlock Char\n";

    // ── Test 4: Forged / Invalid HMAC Response Rejection ─────────────────────
    std::string nonce2 = ble.generate_challenge(client_mac);
    std::string invalid_token = compute_hmac_sha256("wrong_secret_key", nonce2);
    bool auth_fail = ble.verify_response(client_mac, invalid_token, master_secret);
    assert(!auth_fail);
    std::cout << "[PASS] Forged / Tampered HMAC Token rejected as expected\n";

    // ── Test 5: Replay Attack / Nonce Expiration Check ──────────────────────
    std::string nonce3 = ble.generate_challenge(client_mac);
    std::string token3 = compute_hmac_sha256(master_secret, nonce3);
    
    // First attempt succeeds
    assert(ble.verify_response(client_mac, token3, master_secret));
    
    // Immediate replay with same token without new challenge should fail
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ble.generate_challenge(client_mac);
    assert(!ble.verify_response(client_mac, token3, master_secret));
    std::cout << "[PASS] Anti-replay nonce rotation verified\n";

    // ── Test 6: Proximity Threshold (Within 3 meters: RSSI >= -78 dBm) ───────
    assert(ble.get_rssi_threshold() == -78);
    ble.update_rssi(client_mac, -90);
    ble.update_rssi(client_mac, -85);
    ble.update_rssi(client_mac, -82);
    assert(!ble.is_in_actuation_zone(client_mac)); // Beyond 3m (median -85 < -78 dBm)

    // Approaching gateway antenna (< 3m):
    ble.update_rssi(client_mac, -75);
    ble.update_rssi(client_mac, -72);
    ble.update_rssi(client_mac, -70);
    assert(ble.is_in_actuation_zone(client_mac)); // Within 3m (median -72 >= -78 dBm)
    std::cout << "[PASS] Proximity zone (RSSI >= -78 dBm / ~3 meters) verified\n";

    // ── Test 7: Door Status Characteristic & Auto-Unlock Pulse ──────────────
    // Initially latched (0x00)
    assert(ble.get_door_status() == 0x00);
    std::cout << "[PASS] Initial Door Status is 0x00 (Latched)\n";

    // Trigger access with master key and verify unlatch confirmed (0x01)
    std::string unlock_nonce = ble.generate_challenge(client_mac);
    std::string unlock_token = compute_hmac_sha256(master_secret, unlock_nonce);
    bool unlock_ok = ble.trigger_access_if_authorized(client_mac, unlock_token, master_secret);
    assert(unlock_ok);
    assert(ble.get_door_status() == 0x01); // 0x01 = physical unlatch confirmed
    std::cout << "[PASS] Door Status transitions to 0x01 (Physical Unlatch Confirmed)\n";

    // ── Test 8: Status JSON Output & UUID Verification ──────────────────────
    auto st = ble.status_json();
    assert(st["running"] == true);
    assert(st["service_uuid"] == "9F82A001-7B34-4C12-8921-5092A1B2C3D4");
    assert(st["challenge_uuid"] == "9F82A002-7B34-4C12-8921-5092A1B2C3D4");
    assert(st["response_uuid"] == "9F82A003-7B34-4C12-8921-5092A1B2C3D4");
    assert(st["door_status_uuid"] == "9F82A004-7B34-4C12-8921-5092A1B2C3D4");
    assert(st["door_status"] == 0x01);
    assert(st["rssi_threshold"] == -78);
    assert(st.contains("active_clients"));
    std::cout << "[PASS] Service, Challenge, Response, and Door Status UUIDs verified in status JSON\n";

    ble.stop();
    std::cout << "\n===============================================\n";
    std::cout << "  ALL PATH 2 BLE PHYSICAL GATEWAY TESTS PASSED! \n";
    std::cout << "===============================================\n";
    return 0;
}
