#pragma once

#include "senzr/export.hpp"
#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace senzr {
inline namespace v1 {
namespace mqtt {

/**
 * @brief MQTT Quality of Service levels.
 */
enum class QoS {
    AtMostOnce  = 0,
    AtLeastOnce = 1,
    ExactlyOnce = 2
};

/**
 * @brief Secure MQTT connection parameters.
 */
struct SENZR_API MqttConfig {
    std::string broker_host;
    int broker_port{8883}; // Default to MQTTS with TLS
    std::string client_id;
    std::string username;
    std::string password;
    std::string topic_prefix{"access_device/v1"};
    bool use_tls{true};
    std::string ca_cert_path;
    int keep_alive_sec{30};
    bool auto_reconnect{true};
};

/**
 * @brief Normalized MQTT message packet.
 */
struct SENZR_API MqttMessage {
    std::string topic;
    std::string payload;
    QoS qos{QoS::AtLeastOnce};
    bool retain{false};
};

/**
 * @brief Thread-safe, secure IoT MQTT Client with automatic reconnection and TLS support.
 */
class SENZR_API Client {
public:
    explicit Client(MqttConfig config);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    [[nodiscard]] Result<void> connect();
    [[nodiscard]] Result<void> disconnect();
    [[nodiscard]] bool is_connected() const noexcept;

    // ── Messaging ─────────────────────────────────────────────────────────────
    [[nodiscard]] Result<void> publish(const std::string& topic, const std::string& payload, QoS qos = QoS::AtLeastOnce);
    [[nodiscard]] Result<void> subscribe(const std::string& topic, QoS qos = QoS::AtLeastOnce);
    [[nodiscard]] Result<void> unsubscribe(const std::string& topic);

    // ── Callbacks ─────────────────────────────────────────────────────────────
    using MessageHandler = std::function<void(const std::string& topic, const std::string& payload)>;
    using ConnectionHandler = std::function<void(bool connected)>;

    void set_message_handler(MessageHandler handler);
    void set_connection_handler(ConnectionHandler handler);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace mqtt
} // namespace v1
} // namespace senzr
