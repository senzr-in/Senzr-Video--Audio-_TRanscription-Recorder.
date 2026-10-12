#pragma once

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>

namespace app {

class Database;
class GpioManager;

struct BleClientSession {
    std::string device_mac;
    int last_rssi{ -100 };
    std::deque<int> rssi_history;
    std::string current_nonce;
    std::chrono::steady_clock::time_point nonce_created_at;
    bool authenticated{ false };
};

class BleAccessManager {
public:
    static BleAccessManager& instance() {
        static BleAccessManager inst;
        return inst;
    }

    BleAccessManager();
    ~BleAccessManager();

    // ── Service & Characteristic UUID Constants ───────────────────────────────
    static constexpr const char* SERVICE_UUID     = "9F82A001-7B34-4C12-8921-5092A1B2C3D4";
    static constexpr const char* CHALLENGE_UUID   = "9F82A002-7B34-4C12-8921-5092A1B2C3D4";
    static constexpr const char* RESPONSE_UUID    = "9F82A003-7B34-4C12-8921-5092A1B2C3D4";
    static constexpr const char* DOOR_STATUS_UUID = "9F82A004-7B34-4C12-8921-5092A1B2C3D4";

    // Start/Stop the BLE GATT & RSSI background scanner
    bool start(Database* db = nullptr, GpioManager* gpio = nullptr);
    void stop();
    bool is_running() const { return running_.load(); }

    // Generate cryptographic 128-bit challenge nonce for incoming BLE connection
    std::string generate_challenge(const std::string& client_mac);

    // Verify response token (HMAC / AES-GCM tag check with freshness < 5s)
    bool verify_response(const std::string& client_mac,
                         const std::string& client_response,
                         const std::string& credential_hash);

    // RSSI distance smoothing: adds raw RSSI sample and returns smoothed median
    int update_rssi(const std::string& client_mac, int raw_rssi);
    bool is_in_actuation_zone(const std::string& client_mac, int rssi_threshold = 0);

    // Range threshold & auto-unlock configuration (default: -78 dBm for ~3m proximity)
    void set_rssi_threshold(int th);
    int get_rssi_threshold() const;
    void set_auto_unlock(bool en);
    bool get_auto_unlock() const;

    // Door Status: 0x01 = unlatched confirmed, 0x00 = latched
    uint8_t get_door_status() const;
    void set_door_status(uint8_t status);

    // Trigger door unlock if in range and verified
    bool trigger_access_if_authorized(const std::string& client_mac,
                                     const std::string& client_response,
                                     const std::string& credential_hash);

    // Status inspection
    nlohmann::json status_json() const;

private:
    void ble_event_loop();
    int calculate_median_rssi(const std::deque<int>& history) const;

    Database* db_{nullptr};
    GpioManager* gpio_{nullptr};

    std::atomic<bool> running_{false};
    std::atomic<int> rssi_threshold_{ -78 }; // Proximity: within 3 meters (RSSI >= -78 dBm)
    std::atomic<bool> auto_unlock_{ true };
    std::atomic<uint8_t> door_status_{ 0x00 }; // 0x00 = latched, 0x01 = unlatched
    std::chrono::steady_clock::time_point unlatch_expires_at_;
    std::thread worker_thread_;

    mutable std::mutex mutex_;
    std::map<std::string, BleClientSession> sessions_; // MAC -> Session
};

} // namespace app
