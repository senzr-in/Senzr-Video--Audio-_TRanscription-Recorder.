#include "senzr/senzr.h"
#include <iostream>
#include <cassert>
#include <cstring>

int main() {
    std::cout << "=== Running Senzr Pure C API Test Suite ===\n";

    // 1. Version API
    const char* ver_str = senzr_get_version_string();
    assert(ver_str != nullptr);
    assert(std::string(ver_str) == "1.1.0");
    std::cout << "[PASS] Version string: " << ver_str << "\n";

    int maj = 0, min = 0, pat = 0;
    senzr_get_version(&maj, &min, &pat);
    assert(maj == 1 && min == 1 && pat == 0);
    std::cout << "[PASS] Semantic version: " << maj << "." << min << "." << pat << "\n";

    // 2. Logging API
    senzr_log_set_level(2); // Info
    assert(senzr_log_get_level() == 2);
    senzr_log_write(2, "C_API_TEST", "Logging successfully called via pure C ABI");
    std::cout << "[PASS] Logging C API verified\n";

    // 3. Device API
    senzr_device_t dev = senzr_device_get_instance();
    assert(dev != nullptr);

    char serial[128] = {0};
    int err = senzr_device_get_serial_number(dev, serial, sizeof(serial));
    assert(err == SENZR_OK);
    assert(std::strlen(serial) > 0);
    std::cout << "[PASS] Device Serial: " << serial << "\n";

    char mac[64] = {0};
    err = senzr_device_get_mac_address(dev, mac, sizeof(mac));
    assert(err == SENZR_OK);
    std::cout << "[PASS] Device MAC: " << mac << "\n";

    double cpu_temp = 0.0, cpu_usage = 0.0;
    uint64_t ram_used = 0, ram_total = 0;
    err = senzr_device_get_system_metrics(dev, &cpu_temp, &cpu_usage, &ram_used, &ram_total);
    assert(err == SENZR_OK);
    std::cout << "[PASS] System Metrics: Temp=" << cpu_temp << " C, RAM Used=" << ram_used << " MB\n";

    // 4. GPIO API
    senzr_gpio_t gpio = senzr_gpio_get_instance();
    assert(gpio != nullptr);
    err = senzr_gpio_pin_mode(gpio, 18, 1); // Output
    assert(err == SENZR_OK);
    err = senzr_gpio_digital_write(gpio, 18, 1); // High
    assert(err == SENZR_OK);
    int read_val = -1;
    err = senzr_gpio_digital_read(gpio, 18, &read_val);
    assert(err == SENZR_OK);
    std::cout << "[PASS] GPIO C API verified\n";

    // 5. Storage API
    senzr_storage_t storage = senzr_storage_create(":memory:");
    assert(storage != nullptr);
    err = senzr_storage_open(storage);
    assert(err == SENZR_OK);
    assert(senzr_storage_is_open(storage));

    err = senzr_storage_log_event(storage, "evt-001", "sess-100", "ACCESS_GRANTED", "{\"gate\":1}", 0.99f);
    assert(err == SENZR_OK);

    senzr_storage_close(storage);
    assert(!senzr_storage_is_open(storage));
    senzr_storage_destroy(storage);
    std::cout << "[PASS] Storage C API verified\n";

    // 6. MQTT API (creation and lifecycle)
    senzr_mqtt_t mqtt = senzr_mqtt_create("127.0.0.1", 1883, "c_test_client");
    assert(mqtt != nullptr);
    assert(!senzr_mqtt_is_connected(mqtt));
    senzr_mqtt_destroy(mqtt);
    std::cout << "[PASS] MQTT C API verified\n";

    std::cout << "=== All C API Tests PASSED Successfully ===\n";
    return 0;
}
