#include "app/ble_access_manager.hpp"
#include "app/database.hpp"
#include "app/gpio_manager.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace app {

static std::string bytes_to_hex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    return oss.str();
}

BleAccessManager::BleAccessManager() = default;

BleAccessManager::~BleAccessManager() {
    stop();
}

bool BleAccessManager::start(Database* db, GpioManager* gpio) {
    if (running_.load()) return true;

    db_ = db;
    gpio_ = gpio;
    running_.store(true);
    worker_thread_ = std::thread(&BleAccessManager::ble_event_loop, this);

    std::cout << "[BleAccessManager] Started GATT Service UUID: " << SERVICE_UUID << std::endl;
    return true;
}

void BleAccessManager::stop() {
    if (running_.load()) {
        running_.store(false);
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
}

std::string BleAccessManager::generate_challenge(const std::string& client_mac) {
    std::lock_guard<std::mutex> lock(mutex_);

    unsigned char rand_buf[16];
    RAND_bytes(rand_buf, sizeof(rand_buf));
    std::string nonce = bytes_to_hex(rand_buf, sizeof(rand_buf));

    auto& session = sessions_[client_mac];
    session.device_mac = client_mac;
    session.current_nonce = nonce;
    session.nonce_created_at = std::chrono::steady_clock::now();
    session.authenticated = false;

    return nonce;
}

bool BleAccessManager::verify_response(const std::string& client_mac,
                                      const std::string& client_response,
                                      const std::string& credential_hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_mac);
    if (it == sessions_.end()) return false;

    auto& session = it->second;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - session.nonce_created_at).count();

    // Challenge nonce must be answered within 5 seconds to prevent replay attacks
    if (elapsed > 5 || session.current_nonce.empty()) {
        std::cerr << "[BleAccessManager] Nonce expired or invalid for " << client_mac << std::endl;
        return false;
    }

    // Expected token = HMAC-SHA256(key=credential_hash, data=current_nonce)
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    HMAC(EVP_sha256(),
         credential_hash.data(), credential_hash.size(),
         reinterpret_cast<const unsigned char*>(session.current_nonce.data()), session.current_nonce.size(),
         md, &md_len);

    std::string expected_token = bytes_to_hex(md, md_len);
    if (client_response == expected_token) {
        session.authenticated = true;
        std::cout << "[BleAccessManager] Client " << client_mac << " authenticated successfully!" << std::endl;
        return true;
    }

    std::cerr << "[BleAccessManager] Authentication failed for " << client_mac << std::endl;
    return false;
}

int BleAccessManager::calculate_median_rssi(const std::deque<int>& history) const {
    if (history.empty()) return -100;
    std::vector<int> sorted(history.begin(), history.end());
    std::sort(sorted.begin(), sorted.end());
    return sorted[sorted.size() / 2];
}

int BleAccessManager::update_rssi(const std::string& client_mac, int raw_rssi) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& session = sessions_[client_mac];
    session.device_mac = client_mac;

    session.rssi_history.push_back(raw_rssi);
    if (session.rssi_history.size() > 10) {
        session.rssi_history.pop_front();
    }

    session.last_rssi = calculate_median_rssi(session.rssi_history);
    return session.last_rssi;
}

bool BleAccessManager::is_in_actuation_zone(const std::string& client_mac, int rssi_threshold) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_mac);
    if (it != sessions_.end()) {
        return it->second.last_rssi >= rssi_threshold;
    }
    return false;
}

bool BleAccessManager::trigger_access_if_authorized(const std::string& client_mac,
                                                   const std::string& client_response,
                                                   const std::string& credential_hash) {
    if (!verify_response(client_mac, client_response, credential_hash)) {
        return false;
    }

    // Verify credential in database
    if (db_) {
        auto cred = db_->validate_credential_hash(credential_hash);
        if (!cred.has_value()) {
            std::cerr << "[BleAccessManager] Valid HMAC but credential hash not found/revoked in DB: "
                      << credential_hash << std::endl;
            return false;
        }
        std::cout << "[BleAccessManager] Access Granted for: " << cred->holder_name 
                  << " (Role: " << cred->role << ")" << std::endl;
    }

    // Trigger relay pulse
    if (gpio_) {
        gpio_->pulse_alias("DOOR_RELAY", 3000, PinValue::HIGH);
        gpio_->buzzer_beep_alias("BUZZER", 2, 80, 80);
    }

    return true;
}

void BleAccessManager::ble_event_loop() {
    while (running_.load()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto now = std::chrono::steady_clock::now();
            // Prune sessions older than 30s
            for (auto it = sessions_.begin(); it != sessions_.end(); ) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.nonce_created_at).count();
                if (age > 30) {
                    it = sessions_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

nlohmann::json BleAccessManager::status_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json root;
    root["running"] = running_.load();
    root["service_uuid"] = SERVICE_UUID;
    root["challenge_uuid"] = CHALLENGE_UUID;
    root["response_uuid"] = RESPONSE_UUID;

    nlohmann::json client_arr = nlohmann::json::array();
    for (const auto& [mac, session] : sessions_) {
        client_arr.push_back({
            {"mac", mac},
            {"median_rssi", session.last_rssi},
            {"authenticated", session.authenticated}
        });
    }
    root["active_clients"] = client_arr;
    return root;
}

} // namespace app
