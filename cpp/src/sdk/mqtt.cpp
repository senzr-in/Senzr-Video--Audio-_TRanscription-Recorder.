#include "senzr/mqtt/client.hpp"
#include "app/mqtt_access_client.hpp"

namespace senzr {
inline namespace v1 {
namespace mqtt {

struct Client::Impl {
    MqttConfig config;
    std::unique_ptr<app::MqttAccessClient> backend;
    MessageHandler msg_handler;
    ConnectionHandler conn_handler;

    explicit Impl(MqttConfig cfg) : config(std::move(cfg)) {
        app::MqttAccessConfig app_cfg;
        app_cfg.broker_host = config.broker_host;
        app_cfg.broker_port = config.broker_port;
        app_cfg.username    = config.username;
        app_cfg.password    = config.password;
        app_cfg.prefix      = config.topic_prefix;
        app_cfg.uuid        = config.client_id;
        app_cfg.heartbeat_interval_sec = config.keep_alive_sec;
        backend = std::make_unique<app::MqttAccessClient>(app_cfg);
    }
};

Client::Client(MqttConfig config)
    : pimpl_(std::make_unique<Impl>(std::move(config))) {}

Client::~Client() = default;
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;

Result<void> Client::connect() {
    if (pimpl_->config.broker_host.empty()) {
        return Result<void>(ErrorCode::InvalidArgument, "MQTT broker host must not be empty");
    }
    if (pimpl_->backend->start()) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::NetworkError, "Failed to connect to MQTT broker");
}

Result<void> Client::disconnect() {
    pimpl_->backend->stop();
    return Result<void>::success();
}

bool Client::is_connected() const noexcept {
    return pimpl_->backend && pimpl_->backend->is_connected();
}

Result<void> Client::publish(const std::string& topic, const std::string& payload, QoS /*qos*/) {
    if (!is_connected()) {
        return Result<void>(ErrorCode::NotInitialized, "MQTT client is not connected");
    }
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(payload);
    } catch (...) {
        j = {{"payload", payload}};
    }
    if (pimpl_->backend->publish_custom(topic, j)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::NetworkError, "Failed to publish MQTT message to topic: " + topic);
}

Result<void> Client::subscribe(const std::string& /*topic*/, QoS /*qos*/) {
    if (!is_connected()) {
        return Result<void>(ErrorCode::NotInitialized, "MQTT client is not connected");
    }
    return Result<void>::success();
}

Result<void> Client::unsubscribe(const std::string& /*topic*/) {
    return Result<void>::success();
}

void Client::set_message_handler(MessageHandler handler) {
    pimpl_->msg_handler = std::move(handler);
}

void Client::set_connection_handler(ConnectionHandler handler) {
    pimpl_->conn_handler = std::move(handler);
}

} // namespace mqtt
} // namespace v1
} // namespace senzr
