#include "senzr/senzr.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("=== Running Senzr Pure C11 Standalone Test Suite ===\n");

    /* 1. Version API */
    const char* ver_str = senzr_get_version_string();
    assert(ver_str != NULL);
    assert(strcmp(ver_str, "1.1.0") == 0);
    printf("[PASS] Pure C Version: %s\n", ver_str);

    int maj = 0, min = 0, pat = 0;
    senzr_get_version(&maj, &min, &pat);
    assert(maj == 1 && min == 1 && pat == 0);
    printf("[PASS] Semantic version: %d.%d.%d\n", maj, min, pat);

    /* 2. Logging API */
    senzr_log_set_level(2); /* Info */
    assert(senzr_log_get_level() == 2);
    senzr_log_write(2, "C11_TEST", "Logging verified from pure C11 translation unit");
    printf("[PASS] Logging C API verified\n");

    /* 3. Device API */
    senzr_device_t dev = senzr_device_get_instance();
    assert(dev != NULL);

    char serial[128] = {0};
    int err = senzr_device_get_serial_number(dev, serial, sizeof(serial));
    assert(err == SENZR_OK);
    assert(strlen(serial) > 0);
    printf("[PASS] Device Serial: %s\n", serial);

    char mac[64] = {0};
    err = senzr_device_get_mac_address(dev, mac, sizeof(mac));
    assert(err == SENZR_OK);
    printf("[PASS] Device MAC: %s\n", mac);

    double cpu_temp = 0.0, cpu_usage = 0.0;
    uint64_t ram_used = 0, ram_total = 0;
    err = senzr_device_get_system_metrics(dev, &cpu_temp, &cpu_usage, &ram_used, &ram_total);
    assert(err == SENZR_OK);
    printf("[PASS] System Metrics: Temp=%.1f C, RAM=%llu MB\n", cpu_temp, (unsigned long long)ram_used);

    /* 4. GPIO API */
    senzr_gpio_t gpio = senzr_gpio_get_instance();
    assert(gpio != NULL);
    err = senzr_gpio_pin_mode(gpio, 18, 1);
    assert(err == SENZR_OK);
    err = senzr_gpio_digital_write(gpio, 18, 1);
    assert(err == SENZR_OK);
    int read_val = -1;
    err = senzr_gpio_digital_read(gpio, 18, &read_val);
    assert(err == SENZR_OK);
    printf("[PASS] GPIO C API verified\n");

    /* 5. Storage API */
    senzr_storage_t storage = senzr_storage_create(":memory:");
    assert(storage != NULL);
    err = senzr_storage_open(storage);
    assert(err == SENZR_OK);
    assert(senzr_storage_is_open(storage));

    err = senzr_storage_log_event(storage, "c11-001", "sess-c11", "AUTH_PASS", "{\"src\":\"c11\"}", 0.98f);
    assert(err == SENZR_OK);

    senzr_storage_close(storage);
    assert(!senzr_storage_is_open(storage));
    senzr_storage_destroy(storage);
    printf("[PASS] Storage C API verified\n");

    /* 6. MQTT API */
    senzr_mqtt_t mqtt = senzr_mqtt_create("127.0.0.1", 1883, "c11_client");
    assert(mqtt != NULL);
    assert(!senzr_mqtt_is_connected(mqtt));
    senzr_mqtt_destroy(mqtt);
    printf("[PASS] MQTT C API verified\n");

    printf("=== All Pure C11 Tests PASSED Successfully ===\n");
    return 0;
}
