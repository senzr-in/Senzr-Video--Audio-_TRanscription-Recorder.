# Senzr SDK Migration Guide

## Upgrading to v1.1.0 (Phase 1 ABI/API Updates)

This guide summarizes the API and ABI changes introduced in `libsenzr` v1.1.0 to meet commercial embedded SDK standards.

---

### 1. Version Namespace (`inline namespace v1`)

All public classes, structures, enums, and functions under `namespace senzr` are now enclosed within an `inline namespace v1`.
- **Source Compatibility:** `senzr::Device`, `senzr::gpio::Manager`, `senzr::Result<T>`, etc. continue to compile unchanged without any code modifications due to C++ inline namespace lookup rules.
- **ABI Mangling:** Symbol mangling now includes `v1` (e.g. `_ZN5senzr2v16Device12get_instanceEv`), preventing ABI collision with future major revisions.

---

### 2. Export Macros & Hidden Visibility (`SENZR_API`)

The SDK now compiles with hidden visibility flags (`-fvisibility=hidden` and `-fvisibility-inlines-hidden`).
- Only symbols annotated with `SENZR_API` are exported in dynamic/shared library builds.
- Include [`<senzr/export.hpp>`](file:///f:/PI5MAX/cpp/include/senzr/export.hpp) or use the master umbrella header [`<senzr/senzr.hpp>`](file:///f:/PI5MAX/cpp/include/senzr/senzr.hpp).

---

### 3. `senzr::Result<T>` Monadic Extensions (`std::expected` Compatibility)

`senzr::Result<T>` has been updated to be fully API-compatible with C++23 `std::expected<T, Error>`:
- Added dereference operators `operator*` and `operator->`.
- Added monadic combinators: `.transform(F&& f)` and `.and_then(F&& f)`.
- Added `.value_or(default_value)` with rvalue overloads.
- Added `[[nodiscard]]` to encourage strict error checking.

#### Example:
```cpp
// Direct dereference
auto metrics = device.get_system_metrics();
if (metrics) {
    std::cout << "CPU Temp: " << metrics->cpu_temp_c << "\n";
}

// Monadic chaining
auto formatted = device.get_device_info()
    .transform([](const senzr::DeviceInfo& info) {
        return "Serial: " + info.serial_number;
    });
```

---

### 4. Pure C ABI Wrapper (`<senzr/senzr.h>`)

Non-C++ applications or language bindings (C, Python `ctypes`/`cffi`, Rust FFI, Go `cgo`) can now interface directly via [`<senzr/senzr.h>`](file:///f:/PI5MAX/cpp/include/senzr/senzr.h).
- Opaque handles: `senzr_device_t`, `senzr_gpio_t`, `senzr_storage_t`, `senzr_mqtt_t`.
- Explicit lifecycle functions: `senzr_storage_create()`, `senzr_storage_destroy()`, `senzr_mqtt_create()`, `senzr_mqtt_destroy()`.
- Standard integer return error codes (`SENZR_OK`, `SENZR_ERR_*`).

---

### 5. CMake Version Generation and Library SOVERSION

- `include/senzr/version.hpp` is now generated at configure time from `version.hpp.in`.
- Dynamic library `libsenzr.so` now carries `VERSION 1.1.0` and `SOVERSION 1`.
