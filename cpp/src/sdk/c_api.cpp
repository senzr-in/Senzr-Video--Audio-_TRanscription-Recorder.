#include "senzr/senzr.h"
#include "senzr/senzr.hpp"
#include <cstring>
#include <memory>

// Define struct wrappers for opaque handles
struct senzr_device {
    senzr::Device* cpp_instance{nullptr};
};

struct senzr_gpio {
    senzr::gpio::Manager* cpp_instance{nullptr};
};

struct senzr_storage {
    std::unique_ptr<senzr::Storage> cpp_instance;
};

struct senzr_mqtt {
    std::unique_ptr<senzr::mqtt::Client> cpp_instance;
};

// Global singleton handle holders
static struct senzr_device g_c_device{&senzr::Device::get_instance()};
static struct senzr_gpio g_c_gpio{&senzr::gpio::Manager::get_instance()};

static int map_error(const senzr::Error& err) {
    return static_cast<int>(err.code());
}

extern "C" {

const char* senzr_get_version_string(void) {
    return senzr::version_string();
}

void senzr_get_version(int* major, int* minor, int* patch) {
    auto v = senzr::version();
    if (major) *major = v.major;
    if (minor) *minor = v.minor;
    if (patch) *patch = v.patch;
}

const char* senzr_error_string(senzr_error_code_t code) {
    return std::string(senzr::error_code_to_string(static_cast<senzr::ErrorCode>(code))).c_str();
}

void senzr_log_set_level(int level) {
    senzr::log::set_level(static_cast<senzr::log::Level>(level));
}

int senzr_log_get_level(void) {
    return static_cast<int>(senzr::log::get_level());
}

void senzr_log_write(int level, const char* tag, const char* message) {
    if (!tag || !message) return;
    senzr::log::write(static_cast<senzr::log::Level>(level), tag, message);
}

/* ── Device Management ────────────────────────────────────────────────────── */
senzr_device_t senzr_device_get_instance(void) {
    return &g_c_device;
}

int senzr_device_get_serial_number(senzr_device_t dev, char* buffer, size_t max_len) {
    if (!dev || !dev->cpp_instance || !buffer || max_len == 0) {
        return SENZR_ERR_INVALID_ARGUMENT;
    }
    std::string s = dev->cpp_instance->get_serial_number();
    std::strncpy(buffer, s.c_str(), max_len - 1);
    buffer[max_len - 1] = '\0';
    return SENZR_OK;
}

int senzr_device_get_mac_address(senzr_device_t dev, char* buffer, size_t max_len) {
    if (!dev || !dev->cpp_instance || !buffer || max_len == 0) {
        return SENZR_ERR_INVALID_ARGUMENT;
    }
    std::string s = dev->cpp_instance->get_mac_address();
    std::strncpy(buffer, s.c_str(), max_len - 1);
    buffer[max_len - 1] = '\0';
    return SENZR_OK;
}

int senzr_device_get_system_metrics(senzr_device_t dev,
                                    double* cpu_temp,
                                    double* cpu_usage,
                                    uint64_t* ram_used_mb,
                                    uint64_t* ram_total_mb) {
    if (!dev || !dev->cpp_instance) return SENZR_ERR_INVALID_ARGUMENT;
    auto res = dev->cpp_instance->get_system_metrics();
    if (!res) return map_error(res.error());
    if (cpu_temp) *cpu_temp = res->cpu_temp_c;
    if (cpu_usage) *cpu_usage = res->cpu_usage_percent;
    if (ram_used_mb) *ram_used_mb = res->ram_used_mb;
    if (ram_total_mb) *ram_total_mb = res->ram_total_mb;
    return SENZR_OK;
}

int senzr_device_reboot(senzr_device_t dev, int delay_seconds) {
    if (!dev || !dev->cpp_instance) return SENZR_ERR_INVALID_ARGUMENT;
    auto res = dev->cpp_instance->reboot(delay_seconds);
    return res ? SENZR_OK : map_error(res.error());
}

/* ── GPIO & Relays ────────────────────────────────────────────────────────── */
senzr_gpio_t senzr_gpio_get_instance(void) {
    return &g_c_gpio;
}

int senzr_gpio_pin_mode(senzr_gpio_t gpio, int physical_pin, int mode_output) {
    if (!gpio || !gpio->cpp_instance) return SENZR_ERR_INVALID_ARGUMENT;
    auto mode = mode_output ? senzr::gpio::PinMode::Output : senzr::gpio::PinMode::Input;
    auto res = gpio->cpp_instance->pin_mode(physical_pin, mode);
    return res ? SENZR_OK : map_error(res.error());
}

int senzr_gpio_digital_write(senzr_gpio_t gpio, int physical_pin, int value_high) {
    if (!gpio || !gpio->cpp_instance) return SENZR_ERR_INVALID_ARGUMENT;
    auto val = value_high ? senzr::gpio::PinValue::High : senzr::gpio::PinValue::Low;
    auto res = gpio->cpp_instance->digital_write(physical_pin, val);
    return res ? SENZR_OK : map_error(res.error());
}

int senzr_gpio_digital_read(senzr_gpio_t gpio, int physical_pin, int* out_value) {
    if (!gpio || !gpio->cpp_instance || !out_value) return SENZR_ERR_INVALID_ARGUMENT;
    auto res = gpio->cpp_instance->digital_read(physical_pin);
    if (!res) return map_error(res.error());
    *out_value = (*res == senzr::gpio::PinValue::High) ? 1 : 0;
    return SENZR_OK;
}

int senzr_gpio_pulse(senzr_gpio_t gpio, int physical_pin, int duration_ms, int active_high) {
    if (!gpio || !gpio->cpp_instance) return SENZR_ERR_INVALID_ARGUMENT;
    auto val = active_high ? senzr::gpio::PinValue::High : senzr::gpio::PinValue::Low;
    auto res = gpio->cpp_instance->pulse(physical_pin, duration_ms, val);
    return res ? SENZR_OK : map_error(res.error());
}

int senzr_gpio_cancel_pulse(senzr_gpio_t gpio, int physical_pin) {
    if (!gpio || !gpio->cpp_instance) return SENZR_ERR_INVALID_ARGUMENT;
    auto res = gpio->cpp_instance->cancel_pulse(physical_pin);
    return res ? SENZR_OK : map_error(res.error());
}

/* ── Storage ──────────────────────────────────────────────────────────────── */
senzr_storage_t senzr_storage_create(const char* db_path) {
    senzr::StorageConfig cfg;
    if (db_path && std::strlen(db_path) > 0) {
        cfg.db_path = db_path;
    }
    auto storage = new senzr_storage();
    storage->cpp_instance = std::make_unique<senzr::Storage>(cfg);
    return storage;
}

void senzr_storage_destroy(senzr_storage_t storage) {
    if (storage) {
        delete storage;
    }
}

int senzr_storage_open(senzr_storage_t storage) {
    if (!storage || !storage->cpp_instance) return SENZR_ERR_INVALID_ARGUMENT;
    auto res = storage->cpp_instance->open();
    return res ? SENZR_OK : map_error(res.error());
}

bool senzr_storage_is_open(senzr_storage_t storage) {
    if (!storage || !storage->cpp_instance) return false;
    return storage->cpp_instance->is_open();
}

void senzr_storage_close(senzr_storage_t storage) {
    if (storage && storage->cpp_instance) {
        storage->cpp_instance->close();
    }
}

int senzr_storage_log_event(senzr_storage_t storage,
                            const char* event_id,
                            const char* session_id,
                            const char* event_type,
                            const char* payload,
                            float confidence) {
    if (!storage || !storage->cpp_instance) return SENZR_ERR_INVALID_ARGUMENT;
    senzr::StorageEvent ev;
    if (event_id) ev.event_id = event_id;
    if (session_id) ev.session_id = session_id;
    if (event_type) ev.event_type = event_type;
    if (payload) ev.payload = payload;
    ev.confidence = confidence;
    auto res = storage->cpp_instance->log_event(ev);
    return res ? SENZR_OK : map_error(res.error());
}

/* ── MQTT Client ──────────────────────────────────────────────────────────── */
senzr_mqtt_t senzr_mqtt_create(const char* broker_host, int port, const char* client_id) {
    senzr::mqtt::MqttConfig cfg;
    if (broker_host) cfg.broker_host = broker_host;
    if (port > 0) cfg.broker_port = port;
    if (client_id) cfg.client_id = client_id;
    cfg.use_tls = false; // default plain unless TLS configured

    auto mqtt = new senzr_mqtt();
    mqtt->cpp_instance = std::make_unique<senzr::mqtt::Client>(cfg);
    return mqtt;
}

void senzr_mqtt_destroy(senzr_mqtt_t mqtt) {
    if (mqtt) {
        delete mqtt;
    }
}

int senzr_mqtt_connect(senzr_mqtt_t mqtt) {
    if (!mqtt || !mqtt->cpp_instance) return SENZR_ERR_INVALID_ARGUMENT;
    auto res = mqtt->cpp_instance->connect();
    return res ? SENZR_OK : map_error(res.error());
}

int senzr_mqtt_disconnect(senzr_mqtt_t mqtt) {
    if (!mqtt || !mqtt->cpp_instance) return SENZR_ERR_INVALID_ARGUMENT;
    auto res = mqtt->cpp_instance->disconnect();
    return res ? SENZR_OK : map_error(res.error());
}

bool senzr_mqtt_is_connected(senzr_mqtt_t mqtt) {
    if (!mqtt || !mqtt->cpp_instance) return false;
    return mqtt->cpp_instance->is_connected();
}

int senzr_mqtt_publish(senzr_mqtt_t mqtt, const char* topic, const char* payload, int qos) {
    if (!mqtt || !mqtt->cpp_instance || !topic || !payload) return SENZR_ERR_INVALID_ARGUMENT;
    auto res = mqtt->cpp_instance->publish(topic, payload, static_cast<senzr::mqtt::QoS>(qos));
    return res ? SENZR_OK : map_error(res.error());
}

} // extern "C"
