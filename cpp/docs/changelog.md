# Senzr OS C++ SDK Changelog

## [1.1.0] - 2026-10-08

### Major Architecture & Decoupling
- **Complete SDK Decoupling**: Replaced legacy aliases in `include/senzr/` with standalone, fully decoupled public headers.
- **Strict Public Contract**: Isolated all internal app headers (`include/app/*`), SQLite3, libmosquitto, OpenCV, and RKNN headers from consumer visibility.
- **Unified Error Model**: Implemented `senzr::ErrorCode`, `senzr::Error`, and `senzr::Result<T>` monadic error handling across all public methods.
- **Semantic Versioning**: Introduced `senzr::version()` and `senzr::version_string()` (`1.1.0`).
- **Standardized Logging**: Created thread-safe logging subsystem (`senzr::log`) with configurable sinks and log levels.
- **CMake Package Export**: Added `SenzrConfig.cmake`, `SenzrConfigVersion.cmake`, and `SenzrTargets.cmake` with target alias `Senzr::SDK`.
- **External Consumer Gate**: Added `tests/sdk-consumer-test/` verifying external CMake consumers build cleanly without internal repository paths.

### Security Hardening
- **TLS by Default**: Hardened MQTT configuration to port 8883 MQTTS.
- **FOTA Validation**: Enforced mandatory SHA-256 cryptographic verification before flashing firmware bundles.
- **Sanitized Execution**: Hardened watchdog, GPIO, and storage handlers against unverified input.

### Examples & Documentation
- Added 4 complete standalone examples in `examples/01_device_info`, `02_gpio_relay`, `03_storage`, and `04_anpr`.
- Added comprehensive documentation in `docs/` covering architecture, lifecycle, threading, error handling, security, FOTA, and packaging.
