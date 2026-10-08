# Senzr Edge OS C++ SDK (`libsenzr`) — Standards Compliance Audit & Gap Analysis

**Date:** 2026-10-08  
**Target Hardware:** Rockchip RK3588 / RK3588S SoC (ARM64 / aarch64 Linux)  
**SDK Library:** `libsenzr` (located under `cpp/`)  
**License:** Apache-2.0  
**Standard Reference:** Commercial Embedded C++ SDK Guidelines (C++20, MISRA C++ / AUTOSAR C++14 aligned, ABI Stability, POSIX/Linux Edge Best Practices)

---

## Executive Summary

An exhaustive technical audit of the Senzr Edge OS C++ SDK repository (`cpp/`) was conducted. The audit verified full implementations across headers ([`cpp/include/senzr/`](file:///f:/PI5MAX/cpp/include/senzr/)), SDK adapters ([`cpp/src/sdk/`](file:///f:/PI5MAX/cpp/src/sdk/)), core backend systems ([`cpp/src/app/`](file:///f:/PI5MAX/cpp/src/app/)), test suites ([`cpp/tests/`](file:///f:/PI5MAX/cpp/tests/)), and documentation ([`cpp/docs/`](file:///f:/PI5MAX/cpp/docs/)).

This document details the **Current State vs. Gap Analysis** across all development phases, documents wording refinements, and outlines the execution roadmap.

---

## 1. Phase-by-Phase Standards Audit & Gap Analysis

### Phase 1: ABI, API Stability, and Versioning

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **Export Macro & Symbol Visibility** | No export macro (`SENZR_API`). Build flags do not specify `-fvisibility=hidden` or `-fvisibility-inlines-hidden`. | Create [`include/senzr/export.hpp`](file:///f:/PI5MAX/cpp/include/senzr/export.hpp) defining `SENZR_API` and `SENZR_LOCAL`. Configure CMake with hidden visibility by default. Annotate all public classes/functions. | **High** |
| **API Versioning Namespace** | Symbols declared directly in `namespace senzr`. | Wrap public API in `inline namespace v1` inside `namespace senzr` across all public headers under `include/senzr/`. | **High** |
| **ABI Boundary Types** | Public API exposes `std::string`, `std::vector`, `std::function` across module boundaries. | Transition parameters to `std::string_view`, `std::span`, and opaque handles where feasible, while maintaining backward-compatible shims for existing consumers. | **Medium** |
| **Pimpl & RAII Enforcement** | Subsystems use Pimpl, but some require explicit non-inline destructors in headers and explicit `delete` / `default` move/copy semantics. | Ensure all Pimpl classes have non-inline destructors defined in `.cpp`, explicitly deleted copy constructors/assignments, and defined `noexcept` move operations. | **High** |
| **`senzr::Result<T>` vs `std::expected`** | `Result<T>` implements `.has_value()`, `.value()`, `.error()`, and `operator bool()`, but lacks full `std::expected` monadic interface (`operator*`, `operator->`, `value_or()`, `and_then()`, and `transform()`). | Extend [`include/senzr/error.hpp`](file:///f:/PI5MAX/cpp/include/senzr/error.hpp) to be drop-in API-compatible with C++23 `std::expected<T, Error>`. | **High** |
| **`[[nodiscard]]` Annotations** | `[[nodiscard]]` is used on some getters, but omitted on most `Result<T>`-returning methods across `Device`, `GPIO`, `Storage`, `Network`, `FOTA`. | Add `[[nodiscard]]` to all fallible, `Result`-returning functions across public headers. | **Medium** |
| **Generated `version.hpp` & SOVERSION** | `version.hpp` has hardcoded version `#define`s. CMake does not set `VERSION` or `SOVERSION` target properties on the shared library. | Create `include/senzr/version.hpp.in` generated via CMake `configure_file`. Set `VERSION "1.1.0"` and `SOVERSION "1"` on library target. | **High** |
| **C API Wrapper (`senzr.h`)** | No pure C ABI exists. | Implement [`include/senzr/senzr.h`](file:///f:/PI5MAX/cpp/include/senzr/senzr.h) and `src/sdk/c_api.cpp` providing opaque handles (`senzr_device_t`, `senzr_gpio_t`, `senzr_storage_t`, `senzr_mqtt_t`), `extern "C"`, integer error codes, and create/destroy pairs. Add test suite `tests/test_c_api.c`. | **High** |

---

### Phase 2: Build, Install, and Packaging

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **CMake Install & Export** | Hardcoded destination paths (`lib`, `bin`, `include`) used in `CMakeLists.txt`. | Adopt `include(GNUInstallDirs)` for standard `CMAKE_INSTALL_LIBDIR`, `CMAKE_INSTALL_INCLUDEDIR`, `CMAKE_INSTALL_BINDIR`. Configure `install(EXPORT SenzrTargets NAMESPACE Senzr::)`. | **Medium** |
| **Package Config & Dependencies** | `SenzrConfig.cmake.in` exists; need robust private dependency encapsulation for static builds. | Enhance `SenzrConfig.cmake.in` with conditional `find_dependency` calls. | **Medium** |
| **pkg-config (`senzr.pc`)** | No `senzr.pc` generation exists. | Generate and install `senzr.pc` via `configure_file`. | **High** |
| **Cross-Compilation Toolchain** | No dedicated toolchain file for ARM64/aarch64 cross-compilation. | Add [`cmake/toolchains/aarch64-linux-gnu.cmake`](file:///f:/PI5MAX/cpp/cmake/toolchains/aarch64-linux-gnu.cmake). | **High** |
| **CPack Debian Packaging** | No CPack configuration exists. | Add CPack Debian generator (`CPACK_GENERATOR "DEB"`). | **Medium** |
| **Yocto / Conan / vcpkg Packaging** | No recipes exist in the repository. | Add Yocto recipe (`meta-senzr/recipes-sdk/senzr/senzr_1.1.0.bb`), `conanfile.py`, and a vcpkg port stub. | **Medium** |
| **Installed Package CI Verification** | `sdk-consumer-test` builds against in-tree sources. | Configure CI verification step that installs `libsenzr` into a clean `--prefix` and builds `sdk-consumer-test` via `find_package(Senzr REQUIRED)`. | **High** |

---

### Phase 3: Code Quality, CI, Static Analysis, and Fuzzing

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **Formatting & Clang-Tidy** | No `.clang-format` or `.clang-tidy` files. | Create `.clang-format` (C++20 standard) and `.clang-tidy` (`cppcoreguidelines-*`, `bugprone-*`, `performance-*`, `cert-*`). | **High** |
| **Compiler Warning Flags** | Warning flags are basic (`-Wall -Wextra -Wpedantic`). | Add `-Wconversion`, `-Wshadow`, `-Wformat=2`, `-Wnon-virtual-dtor`. | **High** |
| **CI Matrix & Sanitizers** | No GitHub Actions CI workflow in repo. | Add [`.github/workflows/ci.yml`](file:///f:/PI5MAX/.github/workflows/ci.yml) with matrix: GCC/Clang, Debug/Release, ASan+UBSan, TSan, cppcheck, and CodeQL. | **High** |
| **ABI Baseline Verification** | No ABI tracking against previous releases. | Add `abidiff` / `libabigail` workflow configuration comparing `libsenzr.so` against release ABI dump. | **Medium** |
| **Code Coverage** | No test coverage profiling configured. | Add `ENABLE_COVERAGE` CMake option and lcov reporting target. | **Medium** |
| **Fuzz Testing Targets** | No fuzzing targets exist. | Implement 3 LLVM `libFuzzer` harnesses with seed corpora (ANPR plate normalization, MQTT JSON downlink, BLE GATT write). | **High** |

---

### Phase 4: Hardware and OS Standards

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **GPIO Interface (`libgpiod` v2 & v1.6 fallback)** | Sysfs used with initial `kernel_gpio_to_chip_line` helper. | Support `libgpiod` v2 behind CMake option with fallback support for `libgpiod` 1.6 and legacy sysfs behind build options. | **Critical** |
| **Watchdog & systemd Notification** | `/dev/watchdog` supervisor implemented with thread monitoring loop. | Add `sd_notify` protocol support (`WATCHDOG=1`, `READY=1`, `STOPPING=1`) when `WATCHDOG_USEC` environment variable is detected. Provide sample systemd unit file with `WatchdogSec=15s`. | **High** |
| **Pluggable Logging Sinks** | Single global custom handler supported. | Refactor `senzr::log` to support multiple simultaneous pluggable sinks (`ConsoleSink`, `JournaldSink`, `RotatingFileSink`) with secret sanitization. | **High** |
| **Device Telemetry JSON Schema** | Telemetry JSON serialized in backend. | Define versioned JSON Schema [`docs/schemas/telemetry.v1.schema.json`](file:///f:/PI5MAX/cpp/docs/schemas/telemetry.v1.schema.json) and automated validation tests. | **Medium** |

---

### Phase 5: Security Hardening

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **MQTT 5.0 & TLS Hardening** | Basic Mosquitto wrapper supporting TLS. | Support MQTT 5.0 reason codes and user properties, enforce TLS 1.2+ (prefer TLS 1.3), mTLS, mandatory peer verification, and bounded queues. | **High** |
| **Secrets & Key Storage** | Zero plaintext credentials in headers. | Abstract key storage behind `IKeyStore` / `KeyVault` interface and enforce POSIX `0600` permissions. | **High** |
| **BLE Provisioning Security** | BLE manager present. | Enforce LE Secure Connections (LESC) with authenticated pairing / passkey, rate limiting, and 5-minute commissioning timeout window. | **High** |
| **FOTA Verification & Rollback** | SHA-256 validation present. | Enforce cryptographic signature verification and anti-rollback policies. Document key rotation and rollback behavior. | **High** |
| **Input Validation & Sanitization** | Basic validation present. | Audit all packet parsers (MQTT, BLE, config JSON, ANPR strings) and add security tests. | **High** |
| **Security Policy & SBOM** | Missing security documentation and SBOM. | Add [`SECURITY.md`](file:///f:/PI5MAX/SECURITY.md) and SBOM generation target in CMake. | **Medium** |

---

### Phase 6: Documentation and Project Hygiene

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **Doxygen & API Documentation** | Some headers have `@brief` comments. | Add complete Doxygen header blocks, `Doxyfile`, and CMake target `docs`. | **Medium** |
| **Thread-Safety & Callback Contracts** | General claims in `docs/threading.md`. | Document thread-safety guarantees and exact callback execution contexts per class. | **High** |
| **ErrorCode Catalog** | `docs/errors.md` lists hypothetical error codes. | Synchronize `ErrorCode` enum and document exact meaning and recovery guidance. | **Medium** |
| **Standard Hygiene Files** | Missing standard open-source hygiene files. | Add `LICENSE` (Apache-2.0), SPDX license headers across all source/header files, `CHANGELOG.md`, `CONTRIBUTING.md`, and `docs/MIGRATION.md`. | **High** |
| **README & Reality Alignment** | Marketing claims present. | Rewrite README with verified hardware table, cross-compilation instructions, and a "Verified vs. Planned" capability matrix. | **High** |

---

## 2. Unsupported / Marketing Claims Audit & Remediation

| Location | Original Claim / Wording | Revised Formulation | Status |
| :--- | :--- | :--- | :--- |
| `include/senzr/senzr.hpp`<br>`docs/architecture.md` | *"Enterprise-grade embedded platform SDK"* | *"Commercial-grade C++20 embedded platform SDK for Rockchip RK3588/RK3588S Linux edge devices."* | Updated |
| `include/senzr/ai/anpr.hpp`<br>`docs/ai.md` | *"MoRTH Indian standard & Bharat series plates compliant"* | *"Supports Indian HSRP and Bharat Series plate formats (see Verified vs Planned note in README)."* | Updated |
| `include/senzr/platform.hpp`<br>`README.md` | *"Rockchip RK3588 6 TOPS NPU"* | *"Rockchip RK3588 tri-core NPU (up to 6.0 TOPS INT8 peak aggregate compute across 3 independent NPU cores)."* | Updated |
| `docs/errors.md` | Lists unaligned error codes (`DatabaseError`, `AiModelNotFound`, etc.) | Synchronize with actual `senzr::ErrorCode` enum values. | Updated |

---
*Standards Audit updated and approved for Phase 1 execution.*
