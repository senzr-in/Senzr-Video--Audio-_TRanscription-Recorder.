# Getting Started with the Senzr OS C++ SDK (`libsenzr`)

## 1. Overview
The **Senzr OS C++ SDK** (`libsenzr`) provides a unified, production-ready interface for hardware control, offline-first SQLite storage, on-device RK3588 6 TOPS NPU inference, dual-slot RAUC A/B FOTA updates, and cloud IoT connectivity.

## 2. CMake Integration
To use `libsenzr` in your project:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_edge_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(Senzr REQUIRED)

add_executable(my_edge_app main.cpp)
target_link_libraries(my_edge_app PRIVATE Senzr::SDK)
```

## 3. Minimal Application Example

```cpp
#include <senzr/senzr.hpp>
#include <iostream>

int main() {
    // 1. Check SDK Version
    std::cout << "Running on Senzr SDK " << senzr::version_string() << "\n";

    // 2. Query Device Hardware Identity
    auto& dev = senzr::Device::get_instance();
    auto info = dev.get_device_info();
    if (info) {
        std::cout << "Device MAC: " << info.value().mac_address << "\n";
    }

    // 3. Actuate GPIO (3-second non-blocking relay pulse)
    auto& gpio = senzr::gpio::get_manager();
    auto res = gpio.pulse_alias("RELAY", 3000);
    if (!res) {
        std::cerr << "GPIO Error: " << res.error().message() << "\n";
    }

    // 4. Log an Event to Offline SQLite WAL Storage
    senzr::Storage storage;
    if (storage.open()) {
        senzr::StorageEvent ev;
        ev.session_id = "sess_001";
        ev.event_type = "ACCESS_GRANTED";
        ev.timestamp = 1728384000.0;
        storage.log_event(ev);
    }

    return 0;
}
```
