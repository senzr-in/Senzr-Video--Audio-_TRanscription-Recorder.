# Unified Error Model

## Error Architecture

Senzr SDK utilizes a modern C++20 monadic result model:

```cpp
#include <senzr/error.hpp>

senzr::Result<T>
senzr::Result<void>
```

### Error Categories (`senzr::ErrorCode`)

* **Generic**: `Ok`, `Unknown`, `InvalidArgument`, `InvalidState`, `Timeout`, `NotSupported`, `PermissionDenied`
* **Hardware & Device**: `HardwareUnavailable`, `DeviceNotFound`, `GpioError`, `WatchdogError`
* **Network & MQTT**: `NetworkError`, `MqttConnectionFailed`, `MqttPublishFailed`, `TlsError`, `AuthenticationFailed`
* **Storage**: `DatabaseError`, `DiskFull`, `FileNotFound`, `CorruptedData`
* **FOTA**: `FotaDownloadFailed`, `FotaChecksumMismatch`, `FotaSignatureInvalid`, `FotaInstallFailed`
* **AI & Media**: `AiModelNotFound`, `AiInferenceFailed`, `MediaCaptureFailed`, `CodecError`

### Usage Pattern

```cpp
auto res = device.get_device_info();
if (!res) {
    std::cerr << "Failed: " << res.error().message() 
              << " (Code: " << res.error().code_string() << ")\n";
    return;
}
auto info = res.value();
```
