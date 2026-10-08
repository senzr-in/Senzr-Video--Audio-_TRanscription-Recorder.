# Senzr SDK ABI & Pimpl Comprehensive Audit

**Date:** 2026-10-08  
**SDK Version:** 1.1.0  
**Target:** Rockchip RK3588 (aarch64 Linux) & Multi-Platform Edge Appliances

---

## 1. Exported Signatures Using Standard Library Containers

In modern embedded C++ SDKs, standard library containers (`std::string`, `std::vector`, `std::function`) cross the shared library boundary only when ABI constraints are documented and maintained.

| Header | Class / Function | Signature Element | Justification / Current Handling | Migration Plan (Phase 2 & Beyond) |
| :--- | :--- | :--- | :--- | :--- |
| [`device.hpp`](file:///f:/PI5MAX/cpp/include/senzr/device.hpp) | `Device::get_mac_address` | `std::string` return / param | Value-type return string; ABI stable across standard libstdc++ dual ABI. | Provide `std::string_view` parameter overload; C ABI available via `senzr_device_get_mac_address`. |
| [`device.hpp`](file:///f:/PI5MAX/cpp/include/senzr/device.hpp) | `DeviceInfo` struct | `std::string` fields | POD-like identity descriptor struct; serialized to telemetry. | Retain in C++ API; C ABI provides flat char buffers. |
| [`device.hpp`](file:///f:/PI5MAX/cpp/include/senzr/device.hpp) | `Device::control_service` | `std::string` params | Convenience strings for service names and system actions. | Add `std::string_view` overloads in C++20. |
| [`gpio.hpp`](file:///f:/PI5MAX/cpp/include/senzr/gpio.hpp) | `PinInfo` struct | `std::string name`, `default_role`, `alias` | Descriptive names for pin aliases and hardware header roles. | Retain in struct; C ABI provides integer pin handles. |
| [`gpio.hpp`](file:///f:/PI5MAX/cpp/include/senzr/gpio.hpp) | `Manager::get_all_pins` | Returns `std::vector<PinInfo>` | Snapshot vector of discovered 40-pin header configuration. | Transition to `std::span` / iterator accessor in v2. |
| [`gpio.hpp`](file:///f:/PI5MAX/cpp/include/senzr/gpio.hpp) | `Manager::define_alias`, `write_alias`, `pulse_alias` | `std::string alias` | Pin alias lookup key. | Add `std::string_view` overloads. |
| [`storage.hpp`](file:///f:/PI5MAX/cpp/include/senzr/storage.hpp) | `StorageConfig` / `StorageEvent` / `StorageUploadJob` | `std::string` fields | Paths, session IDs, payloads, and error strings. | Retain in C++ value types; C ABI provides flat const char* pointers. |
| [`storage.hpp`](file:///f:/PI5MAX/cpp/include/senzr/storage.hpp) | `Storage::get_recent_events` | Returns `std::vector<StorageEvent>` | Event record collection. | Retain for batch retrieval; C ABI provides iterative callback. |
| [`network.hpp`](file:///f:/PI5MAX/cpp/include/senzr/network.hpp) | `EthernetStatus` / `WifiStatus` / `WifiNetwork` | `std::string` fields | IP, MAC, SSID, Gateway strings. | Retain in struct; C ABI provides flat char buffers. |
| [`network.hpp`](file:///f:/PI5MAX/cpp/include/senzr/network.hpp) | `Manager::scan_wifi` | Returns `std::vector<WifiNetwork>` | Discovery list of Wi-Fi Access Points. | Retain vector return for convenient caller iteration. |
| [`network.hpp`](file:///f:/PI5MAX/cpp/include/senzr/network.hpp) | `Manager::connect_wifi`, `start_hotspot` | `std::string` params | SSIDs and WPA2 passphrases. | Add `std::string_view` overloads. |
| [`ble.hpp`](file:///f:/PI5MAX/cpp/include/senzr/ble.hpp) | `BleConfig` / `BleStatus` | `std::string` fields | Device advertising name and UUID. | Add `std::string_view` constructor overloads. |
| [`fota.hpp`](file:///f:/PI5MAX/cpp/include/senzr/fota.hpp) | `ReleaseMetadata` / `FotaStatus` / `FotaConfig` | `std::string` fields | Versions, SHA-256 hashes, URLs, directory paths. | Retain in struct. |
| [`logging.hpp`](file:///f:/PI5MAX/cpp/include/senzr/logging.hpp) | `log::LogHandler` | `std::function<void(...)>` | Custom logging hook. Callback parameters use `std::string_view`. | Provide C function pointer callback `senzr_log_handler_fn` in C ABI. |
| [`mqtt/client.hpp`](file:///f:/PI5MAX/cpp/include/senzr/mqtt/client.hpp) | `MqttConfig` / `MqttMessage` | `std::string` fields | Broker host, credentials, topic strings. | Retain in struct; C API `senzr_mqtt_create` accepts `const char*`. |
| [`mqtt/client.hpp`](file:///f:/PI5MAX/cpp/include/senzr/mqtt/client.hpp) | `Client::MessageHandler`, `ConnectionHandler` | `std::function` | Asynchronous callback signatures. | Retain for C++ lambda flexibility; C API provides plain function pointer hooks. |
| [`ai/types.hpp`](file:///f:/PI5MAX/cpp/include/senzr/ai/types.hpp) | `Detection` / `PlateInfo` | `std::string` fields | Class label ("person"), formatted plate string. | Retain in value structs. |
| [`ai/inference.hpp`](file:///f:/PI5MAX/cpp/include/senzr/ai/inference.hpp) | `TensorShape` | `std::vector<int64_t>` dims | Multi-dimensional tensor shapes. | Retain in `TensorShape`; add `std::span<const int64_t>` constructor. |
| [`ai/inference.hpp`](file:///f:/PI5MAX/cpp/include/senzr/ai/inference.hpp) | `InferenceResult` | `std::vector<Tensor>` | Batch of output tensors. | Retain. |
| [`ai/inference.hpp`](file:///f:/PI5MAX/cpp/include/senzr/ai/inference.hpp) | `InferenceEngine::infer` | `std::vector<Tensor>` inputs | Multi-input tensor array. | Add `std::span<const Tensor>` overload. |
| [`ai/person_detector.hpp`](file:///f:/PI5MAX/cpp/include/senzr/ai/person_detector.hpp) | `PersonDetector::detect` | Returns `std::vector<Detection>` | Detected bounding boxes. | Retain. |
| [`media/camera.hpp`](file:///f:/PI5MAX/cpp/include/senzr/media/camera.hpp) | `CameraInfo` | `std::vector<VideoResolution>`, `std::vector<PixelFormat>` | Discovered format capabilities. | Retain. |
| [`media/camera.hpp`](file:///f:/PI5MAX/cpp/include/senzr/media/camera.hpp) | `CameraManager::discover_cameras` | Returns `std::vector<CameraInfo>` | Discovered camera list. | Retain. |

---

## 2. Pimpl Encapsulation & Rule-of-Five Audit

Every heavy subsystem in `libsenzr` must satisfy:
1. **Opaque Implementation Pointer:** `std::unique_ptr<Impl> pimpl_` (or `std::shared_ptr<Impl>` for shared value buffers like `Tensor`).
2. **Non-Inline Destructor:** Declared in header, defined in `.cpp` where `struct Impl` definition is complete.
3. **Copy Constructor & Copy Assignment:** Explicitly `= delete;` (or `= default` for value types like `Tensor`).
4. **Move Constructor & Move Assignment:** Explicitly declared `noexcept` and defined in `.cpp`.
5. **No Third-Party Header Leak:** Public header includes zero implementation headers (`sqlite3.h`, `mosquitto.h`, `rknn_api.h`, `v4l2.h`, etc.).

### Comprehensive Verification Matrix

| Class Name | Header Path | Non-Inline Destructor | Copy Deleted? | Move Defined (`noexcept`)? | Pimpl Status | Leak Check |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `senzr::Device` | [`device.hpp`](file:///f:/PI5MAX/cpp/include/senzr/device.hpp) | **YES** (`device.cpp:40`) | **YES** (`= delete`) | **YES** (`device.cpp:41-42`) | **PASS** | **CLEAN** |
| `senzr::gpio::Manager` | [`gpio.hpp`](file:///f:/PI5MAX/cpp/include/senzr/gpio.hpp) | **YES** (`gpio.cpp:12`) | **YES** (`= delete`) | **YES** (`gpio.cpp:13-14`) | **PASS** | **CLEAN** |
| `senzr::Storage` | [`storage.hpp`](file:///f:/PI5MAX/cpp/include/senzr/storage.hpp) | **YES** (`storage.cpp:20`) | **YES** (`= delete`) | **YES** (`storage.cpp:21-22`) | **PASS** | **CLEAN** |
| `senzr::net::Manager` | [`network.hpp`](file:///f:/PI5MAX/cpp/include/senzr/network.hpp) | **YES** (`network.cpp:12`) | **YES** (`= delete`) | **YES** (`network.cpp:13-14`) | **PASS** | **CLEAN** |
| `senzr::watchdog::Supervisor` | [`watchdog.hpp`](file:///f:/PI5MAX/cpp/include/senzr/watchdog.hpp) | **YES** (`watchdog.cpp:14`) | **YES** (`= delete`) | **YES** (`watchdog.cpp:15-16`) | **PASS** | **CLEAN** |
| `senzr::ble::Manager` | [`ble.hpp`](file:///f:/PI5MAX/cpp/include/senzr/ble.hpp) | **YES** (`ble.cpp:12`) | **YES** (`= delete`) | **YES** (`ble.cpp:13-14`) | **PASS** | **CLEAN** |
| `senzr::fota::Client` | [`fota.hpp`](file:///f:/PI5MAX/cpp/include/senzr/fota.hpp) | **YES** (`fota.cpp:25`) | **YES** (`= delete`) | **YES** (`fota.cpp:26-27`) | **PASS** | **CLEAN** |
| `senzr::mqtt::Client` | [`mqtt/client.hpp`](file:///f:/PI5MAX/cpp/include/senzr/mqtt/client.hpp) | **YES** (`mqtt.cpp:29`) | **YES** (`= delete`) | **YES** (`mqtt.cpp:30-31`) | **PASS** | **CLEAN** |
| `senzr::ai::Tensor` | [`ai/inference.hpp`](file:///f:/PI5MAX/cpp/include/senzr/ai/inference.hpp) | **YES** (`ai_inference.cpp:43`) | Copyable (Shared Impl) | **YES** (`ai_inference.cpp:46`) | **PASS** | **CLEAN** |
| `senzr::ai::InferenceEngine` | [`ai/inference.hpp`](file:///f:/PI5MAX/cpp/include/senzr/ai/inference.hpp) | **YES** (`ai_inference.cpp:131`) | **YES** (`= delete`) | **YES** (`ai_inference.cpp:132-133`) | **PASS** | **CLEAN** |
| `senzr::ai::PersonDetector` | [`ai/person_detector.hpp`](file:///f:/PI5MAX/cpp/include/senzr/ai/person_detector.hpp) | **YES** (`ai.cpp:19`) | **YES** (`= delete`) | **YES** (`ai.cpp:20-21`) | **PASS** | **CLEAN** |
| `senzr::media::CameraManager` | [`media/camera.hpp`](file:///f:/PI5MAX/cpp/include/senzr/media/camera.hpp) | **YES** (`camera.cpp:66`) | **YES** (`= delete`) | **YES** (`camera.cpp:67-68`) | **PASS** | **CLEAN** |
| `senzr::media::Recorder` | [`media/recorder.hpp`](file:///f:/PI5MAX/cpp/include/senzr/media/recorder.hpp) | **YES** (`media.cpp:20`) | **YES** (`= delete`) | **YES** (`media.cpp:21-22`) | **PASS** | **CLEAN** |
