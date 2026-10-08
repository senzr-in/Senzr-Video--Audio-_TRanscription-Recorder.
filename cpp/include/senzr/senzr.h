#ifndef SENZR_C_API_H
#define SENZR_C_API_H

/**
 * @file senzr.h
 * @brief Senzr Edge OS SDK — Pure C ABI Interface
 *
 * Provides a stable, opaque-handle C interface with error codes, create/destroy
 * semantics, and zero C++ header dependencies for C, Python (ctypes/cffi),
 * Rust FFI, and Go cgo bindings.
 *
 * Note: While this header is strictly pure C (C11 compliant), the underlying
 * shared/static library (libsenzr) is implemented in C++20. Therefore, pure C
 * applications linking against libsenzr must link the C++ standard library runtime
 * (e.g. -lstdc++ with GCC/Clang or libc++ with Clang).
 */

#include "senzr/export.hpp"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error Codes ──────────────────────────────────────────────────────────── */
typedef enum senzr_error_code {
    SENZR_OK = 0,
    SENZR_ERR_INVALID_ARGUMENT = 1,
    SENZR_ERR_INVALID_STATE = 2,
    SENZR_ERR_NOT_INITIALIZED = 3,
    SENZR_ERR_ALREADY_INITIALIZED = 4,
    SENZR_ERR_NOT_SUPPORTED = 5,
    SENZR_ERR_PERMISSION_DENIED = 6,
    SENZR_ERR_TIMEOUT = 7,
    SENZR_ERR_NETWORK = 8,
    SENZR_ERR_STORAGE = 9,
    SENZR_ERR_HARDWARE = 10,
    SENZR_ERR_FIRMWARE = 11,
    SENZR_ERR_UPDATE_FAILED = 12,
    SENZR_ERR_RESOURCE_UNAVAILABLE = 13,
    SENZR_ERR_INTERNAL = 14
} senzr_error_code_t;

/* ── Opaque Subsystem Handles ─────────────────────────────────────────────── */
typedef struct senzr_device* senzr_device_t;
typedef struct senzr_gpio* senzr_gpio_t;
typedef struct senzr_storage* senzr_storage_t;
typedef struct senzr_mqtt* senzr_mqtt_t;

/* ── Version & Information ────────────────────────────────────────────────── */
SENZR_API const char* senzr_get_version_string(void);
SENZR_API void senzr_get_version(int* major, int* minor, int* patch);
SENZR_API const char* senzr_error_string(senzr_error_code_t code);

/* ── Logging ──────────────────────────────────────────────────────────────── */
SENZR_API void senzr_log_set_level(int level);
SENZR_API int senzr_log_get_level(void);
SENZR_API void senzr_log_write(int level, const char* tag, const char* message);

/* ── Device Management ────────────────────────────────────────────────────── */
SENZR_API senzr_device_t senzr_device_get_instance(void);
SENZR_API int senzr_device_get_serial_number(senzr_device_t dev, char* buffer, size_t max_len);
SENZR_API int senzr_device_get_mac_address(senzr_device_t dev, char* buffer, size_t max_len);
SENZR_API int senzr_device_get_system_metrics(senzr_device_t dev,
                                             double* cpu_temp,
                                             double* cpu_usage,
                                             uint64_t* ram_used_mb,
                                             uint64_t* ram_total_mb);
SENZR_API int senzr_device_reboot(senzr_device_t dev, int delay_seconds);

/* ── GPIO & Relays ────────────────────────────────────────────────────────── */
SENZR_API senzr_gpio_t senzr_gpio_get_instance(void);
SENZR_API int senzr_gpio_pin_mode(senzr_gpio_t gpio, int physical_pin, int mode_output);
SENZR_API int senzr_gpio_digital_write(senzr_gpio_t gpio, int physical_pin, int value_high);
SENZR_API int senzr_gpio_digital_read(senzr_gpio_t gpio, int physical_pin, int* out_value);
SENZR_API int senzr_gpio_pulse(senzr_gpio_t gpio, int physical_pin, int duration_ms, int active_high);
SENZR_API int senzr_gpio_cancel_pulse(senzr_gpio_t gpio, int physical_pin);

/* ── Storage ──────────────────────────────────────────────────────────────── */
SENZR_API senzr_storage_t senzr_storage_create(const char* db_path);
SENZR_API void senzr_storage_destroy(senzr_storage_t storage);
SENZR_API int senzr_storage_open(senzr_storage_t storage);
SENZR_API bool senzr_storage_is_open(senzr_storage_t storage);
SENZR_API void senzr_storage_close(senzr_storage_t storage);
SENZR_API int senzr_storage_log_event(senzr_storage_t storage,
                                      const char* event_id,
                                      const char* session_id,
                                      const char* event_type,
                                      const char* payload,
                                      float confidence);

/* ── MQTT Client ──────────────────────────────────────────────────────────── */
SENZR_API senzr_mqtt_t senzr_mqtt_create(const char* broker_host, int port, const char* client_id);
SENZR_API void senzr_mqtt_destroy(senzr_mqtt_t mqtt);
SENZR_API int senzr_mqtt_connect(senzr_mqtt_t mqtt);
SENZR_API int senzr_mqtt_disconnect(senzr_mqtt_t mqtt);
SENZR_API bool senzr_mqtt_is_connected(senzr_mqtt_t mqtt);
SENZR_API int senzr_mqtt_publish(senzr_mqtt_t mqtt, const char* topic, const char* payload, int qos);

#ifdef __cplusplus
}
#endif

#endif /* SENZR_C_API_H */
