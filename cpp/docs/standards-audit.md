# Senzr Edge OS C++ SDK (`libsenzr`) — Standards Compliance Audit & Gap Analysis

**Date:** 2026-10-08  
**Target Hardware:** Rockchip RK3588 / RK3588S SoC (ARM64 / aarch64 Linux)  
**SDK Library:** `libsenzr` (located under `cpp/`)  
**Standard Reference:** Commercial Embedded C++ SDK Guidelines (C++20, MISRA C++ / AUTOSAR C++14 aligned, ABI Stability, POSIX/Linux Edge Best Practices)

---

## Executive Summary

An exhaustive technical audit of the Senzr Edge OS C++ SDK repository (`cpp/`) was conducted. While the SDK already demonstrates strong architectural modularity (Pimpl encapsulation in several modules, header isolation, and monadic `Result<T>` error handling), several critical gaps must be addressed to bring it to commercial embedded SDK standards.

This document details the **Current State vs. Gap Analysis** across all 6 development phases, identifies claims requiring qualification/softening, and establishes the execution plan for Phases 1 through 6.

---

## 1. Phase-by-Phase Standards Audit & Gap Analysis

### Phase 1: ABI, API Stability, and Versioning

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **Export Macro & Symbol Visibility** | No export macro (`SENZR_API`). Build flags do not specify `-fvisibility=hidden` or `-fvisibility-inlines-hidden`. All public and internal symbols are exported in dynamic libraries. | Create [`include/senzr/export.hpp`](file:///f:/PI5MAX/cpp/include/senzr/export.hpp) defining `SENZR_API` and `SENZR_LOCAL`. Configure CMake with hidden visibility by default. Annotate all public classes/functions. | **High** |
| **API Versioning Namespace** | Symbols are declared directly in `namespace senzr` without version inline namespaces. | Wrap public API in `inline namespace v1` inside `namespace senzr` across all public headers under `include/senzr/`. | **High** |
| **ABI Boundary Types** | Public API exposes `std::string`, `std::vector`, `std::function` across module boundaries (e.g. `MqttConfig`, `get_all_pins()`, `scan_wifi()`, log callbacks). | Review and optimize exported signatures. Transition to `std::string_view`, `std::span`, and opaque handles where feasible, while documenting necessary std containers with defined ABI constraints in migration notes. | **Medium** |
| **Pimpl & RAII Enforcement** | `Device`, `Manager` (GPIO), `Storage`, `Client` (MQTT), `Client` (FOTA), `PersonDetector`, `Recorder`, `Supervisor` use Pimpl, but some lack explicit non-inline destructors in headers, or lack explicit `delete` / `default` move/copy semantics. | Ensure all Pimpl classes have non-inline destructors defined in `.cpp`, explicitly deleted copy constructors/assignments, and correctly defined `noexcept` move operations. | **High** |
| **`senzr::Result<T>` vs `std::expected`** | `Result<T>` implements `.has_value()`, `.value()`, `.error()`, and `operator bool()`, but lacks full `std::expected` monadic interface: `operator*`, `operator->`, `value_or()`, `and_then()`, and `transform()`. | Extend [`include/senzr/error.hpp`](file:///f:/PI5MAX/cpp/include/senzr/error.hpp) to be a drop-in API-compatible precursor to C++23 `std::expected<T, Error>`. | **High** |
| **`[[nodiscard]]` Annotations** | `[[nodiscard]]` is used intermittently (`version()`, `is_open()`, `is_connected()`), but omitted on most `Result<T>`-returning methods across `Device`, `GPIO`, `Storage`, `Network`, `FOTA`. | Add `[[nodiscard]]` to all fallible, `Result`-returning functions across public headers. | **Medium** |
| **Generated `version.hpp` & SOVERSION** | `version.hpp` has hardcoded version `#define`s. CMake target `senzr` does not set `VERSION` or `SOVERSION` target properties. | Create `include/senzr/version.hpp.in` generated via CMake `configure_file`. Set `VERSION "1.1.0"` and `SOVERSION "1"` on library target. | **High** |
| **C API Wrapper (`senzr.h`)** | No C API exists. Non-C++ consumers (C, Python ctypes/cffi, Rust FFI, Go cgo) cannot directly link against `libsenzr`. | Implement [`include/senzr/senzr.h`](file:///f:/PI5MAX/cpp/include/senzr/senzr.h) and `src/sdk/c_api.cpp` providing opaque handles (`senzr_device_t`, `senzr_gpio_t`, `senzr_storage_t`, `senzr_mqtt_t`), `extern "C"`, integer error codes, and create/destroy pairs. Add test suite `tests/test_c_api.c`. | **High** |

---

### Phase 2: Build, Install, and Packaging

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **CMake Install & Export** | Hardcoded destination paths (`lib`, `bin`, `include`) used in `CMakeLists.txt`. | Adopt `include(GNUInstallDirs)` for standard `CMAKE_INSTALL_LIBDIR`, `CMAKE_INSTALL_INCLUDEDIR`, `CMAKE_INSTALL_BINDIR`. Configure `install(EXPORT SenzrTargets NAMESPACE Senzr::)`. | **Medium** |
| **Package Config & Dependencies** | `SenzrConfig.cmake.in` exists but lacks private dependency encapsulation for static builds. `write_basic_package_version_file` exists. | Enhance `SenzrConfig.cmake.in` with conditional `find_dependency` calls. | **Medium** |
| **pkg-config (`senzr.pc`)** | No `senzr.pc` generation exists. | Generate and install `senzr.pc` via `configure_file` for standard Linux build environments. | **High** |
| **Cross-Compilation Toolchain** | No dedicated toolchain file for ARM64/aarch64 cross-compilation from x86_64 build hosts. | Add [`cmake/toolchains/aarch64-linux-gnu.cmake`](file:///f:/PI5MAX/cpp/cmake/toolchains/aarch64-linux-gnu.cmake) with sysroot and target architecture definitions. | **High** |
| **CPack Debian Packaging** | No CPack configuration exists. | Add CPack Debian generator (`CPACK_GENERATOR "DEB"`) with package metadata, dependencies (`libc6`, `libstdc++6`, `libsqlite3-0`, `libgpiod2`), and architecture `arm64`. | **Medium** |
| **Yocto / Conan / vcpkg Packaging** | No recipes exist in the repository. | Add Yocto recipe (`meta-senzr/recipes-sdk/senzr/senzr_1.1.0.bb`), `conanfile.py`, and a vcpkg port stub with TODO notes. | **Medium** |
| **Installed Package CI Verification** | `sdk-consumer-test` exists but builds against in-tree sources. | Configure CI verification step that installs `libsenzr` into a clean `--prefix /tmp/senzr_install` and builds `sdk-consumer-test` strictly via `find_package(Senzr REQUIRED)`. | **High** |

---

### Phase 3: Code Quality, CI, Static Analysis, and Fuzzing

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **Formatting & Clang-Tidy** | No `.clang-format` or `.clang-tidy` files in repository. | Create `.clang-format` (LLVM/Google base, C++20 standard) and `.clang-tidy` (enabling `cppcoreguidelines-*`, `bugprone-*`, `performance-*`, `cert-*`). | **High** |
| **Compiler Warning Flags** | Warning flags are basic (`-Wall -Wextra -Wpedantic`). | Add `-Wconversion`, `-Wshadow`, `-Wformat=2`, `-Wnon-virtual-dtor`, `-Wold-style-cast` and resolve all resulting warnings. | **High** |
| **CI Matrix & Sanitizers** | No GitHub Actions CI workflow in repo. | Add [`.github/workflows/ci.yml`](file:///f:/PI5MAX/.github/workflows/ci.yml) with matrix: GCC/Clang, Debug/Release, ASan (AddressSanitizer) + UBSan (UndefinedBehaviorSanitizer), TSan (ThreadSanitizer), cppcheck, and CodeQL. | **High** |
| **ABI Baseline Verification** | No ABI tracking against previous releases. | Add `abidiff` / `libabigail` workflow configuration comparing `libsenzr.so` against release ABI dump. | **Medium** |
| **Code Coverage** | No test coverage profiling configured. | Add `ENABLE_COVERAGE` CMake option configuring `-fprofile-arcs -ftest-coverage` and lcov/genhtml reporting target. | **Medium** |
| **Fuzz Testing Targets** | No fuzzing targets exist. | Implement 3 LLVM `libFuzzer` harnesses with seed corpora: <br>1. ANPR plate string normalization & regex parsing<br>2. MQTT JSON downlink command parsing<br>3. BLE characteristic write buffer parsing | **High** |

---

### Phase 4: Hardware and OS Standards

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **GPIO Interface (`libgpiod` v2)** | Current implementation uses legacy `/sys/class/gpio` sysfs interface (deprecated since Linux 4.8). | Upgrade GPIO subsystem to **`libgpiod` v2 character device API** (`/dev/gpiochip*`) with non-blocking edge event polling. Retain sysfs behind optional `SENZR_ENABLE_SYSFS_FALLBACK` compile definition with runtime deprecation log. | **Critical** |
| **Watchdog & systemd Notification** | Supervisor directly accesses `/dev/watchdog` but has no `systemd` supervisor integration. | Add `sd_notify` protocol support (`WATCHDOG=1`, `READY=1`, `STOPPING=1`) when `WATCHDOG_USEC` environment variable is detected. Provide sample systemd unit file with `WatchdogSec=15s`. | **High** |
| **Pluggable Logging Sinks** | Logger outputs to `std::cout` with single global custom callback. | Refactor `senzr::log` to support multiple simultaneous pluggable sinks: `ConsoleSink`, `JournaldSink` (systemd journal/syslog), and `RotatingFileSink` (max size + backup count). Enforce automated sanitization of sensitive keys (tokens, passwords). | **High** |
| **Device Telemetry JSON Schema** | Telemetry serialized ad-hoc; no formal contract or schema validation. | Define versioned JSON Schema [`docs/schemas/telemetry.v1.schema.json`](file:///f:/PI5MAX/cpp/docs/schemas/telemetry.v1.schema.json). Add unit test validating SDK-emitted telemetry against this schema. | **Medium** |

---

### Phase 5: Security Hardening

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **MQTT 5.0 & TLS Hardening** | Basic Mosquitto wrapper supporting TLS. Default CA cert path optional. | Support MQTT 5.0 reason codes and user properties. Enforce TLS 1.2 minimum (prefer TLS 1.3), mutual TLS (mTLS client certificate + private key), mandatory server hostname verification (`MOSQ_OPT_TLS_VERIFY_PEER`), bounded publisher queues with backpressure drop policies. | **High** |
| **Secrets & Key Storage** | Configuration files read from filesystem without permission verification. | Perform repo-wide credential scan (confirming 0 hardcoded secrets). Abstract key storage behind `IKeyStore` / `KeyVault` interface for future TPM 2.0 / OTP hardware secure elements. Enforce POSIX `0600` permissions on local private keys. | **High** |
| **BLE Provisioning Security** | BLE manager is a basic stub without security parameters. | Enforce LE Secure Connections (LESC) with authenticated pairing / passkey, sliding-window rate limiting on pairing attempts, strict packet length bounds, and 5-minute commissioning timeout window. | **High** |
| **FOTA Verification & Rollback** | FOTA checks SHA-256 and calls RAUC or tar extraction. | Enforce cryptographic signature verification (RAUC PKI / OpenSSL CMS) prior to flashing. Prevent version downgrades unless explicitly overridden. Document key rotation and rollback behavior. | **High** |
| **Input Validation & Sanitization** | Basic validation in string helpers; lack of systematic bounds/overflow checks across packet parsers. | Audit all parsing routines (MQTT downlink JSON, BLE GATT writes, config values, ANPR text strings). Add test suite `tests/test_security_inputs.cpp`. | **High** |
| **Security Policy & SBOM** | No `SECURITY.md` or Software Bill of Materials (SBOM). | Add [`SECURITY.md`](file:///f:/PI5MAX/SECURITY.md) with vulnerability disclosure process and CVSS triage guidelines. Add CycloneDX / SPDX SBOM generation target in CMake. | **Medium** |

---

### Phase 6: Documentation and Project Hygiene

| Item | Current State | Gap / Required Remediation | Severity |
| :--- | :--- | :--- | :--- |
| **Doxygen & API Documentation** | Some headers have `@brief` comments, but lack `@threadsafe`, `@param`, `@return`, lifetime ownership, error code documentation, and code examples. | Add complete Doxygen header blocks across all public headers. Add `Doxyfile` and CMake target `docs`. | **Medium** |
| **Thread-Safety & Callback Contracts** | General claims in `docs/threading.md`, but individual header classes do not declare exact thread execution contexts for callbacks. | Document thread-safety guarantees and exact callback execution contexts (e.g. "runs on MQTT network worker thread; must not block or invoke reentrant SDK mutation") for each class. | **High** |
| **ErrorCode Catalog** | `docs/errors.md` lists hypothetical error codes not matching `include/senzr/error.hpp`. | Synchronize `ErrorCode` enum and document exact meaning, error trigger conditions, and caller recovery guidance for every code. | **Medium** |
| **Standard Hygiene Files** | No `LICENSE`, `CONTRIBUTING.md`, `CHANGELOG.md`, `MIGRATION.md`. | Add `LICENSE` (Apache-2.0), SPDX license headers across all source/header files, `CHANGELOG.md` (Keep a Changelog format), `CONTRIBUTING.md`, and `docs/MIGRATION.md`. | **High** |
| **README & Reality Alignment** | README contains enthusiastic marketing language without verification tables. | Rewrite README: include verified hardware table, cross-compilation instructions, CMake `find_package` guide, and a "Verified vs. Planned" capability matrix. | **High** |

---

## 2. Unsupported / Marketing Claims Audit & Remediation

To comply with strict engineering integrity standards (Ground Rule 5), the following unsupported claims across existing documentation and comments must be softened or backed with factual engineering citations:

| Location | Original Claim / Wording | Engineering Reality & Required Revision |
| :--- | :--- | :--- |
| `include/senzr/senzr.hpp`<br>`docs/architecture.md` | *"Enterprise-grade embedded platform SDK"* | **Softened:** *"Commercial-grade C++20 embedded platform SDK for Rockchip RK3588/RK3588S Linux edge devices."* |
| `include/senzr/ai/anpr.hpp`<br>`docs/ai.md` | *"MoRTH Indian standard & Bharat series plates compliant"* | **Clarified:** *"Pattern recognition, OCR ambiguity disambiguation, and regex syntax normalization for Indian High Security Registration Plates (HSRP) and Bharat Series (BH) registrations in accordance with MoRTH Gazette Notification GSR 1162(E) and AIS-159 standard formats."* |
| `include/senzr/platform.hpp`<br>`README.md` | *"Rockchip RK3588 6 TOPS NPU"* | **Specified:** *"Rockchip RK3588 tri-core NPU (up to 6.0 TOPS INT8 peak aggregate compute across 3 independent NPU cores)."* |
| `docs/errors.md` | Lists unaligned error codes (`DatabaseError`, `AiModelNotFound`, etc.) | **Synchronized:** Synchronize with actual `senzr::ErrorCode` enum values. |

---

## 3. Implementation Plan & Execution Sequence

Upon approval, implementation will proceed strictly phase-by-phase. Each phase will include compilation, execution of all unit and integration test suites, and validation of zero header leakage:

```
┌────────────────────────┐
│ Phase 1: ABI & API     │ ──► SENZR_API, inline namespace v1, std::expected Result<T>,
│                        │     [[nodiscard]], C API (senzr.h), generated version.hpp
├────────────────────────┤
│ Phase 2: Build & Pkg   │ ──► GNUInstallDirs, CMake export, senzr.pc, aarch64 toolchain,
│                        │     CPack DEB, Yocto recipe, Conan, installed-package test
├────────────────────────┤
│ Phase 3: Quality & CI  │ ──► .clang-format, .clang-tidy, strict warning flags, CI matrix,
│                        │     coverage, 3 libFuzzer targets with seed corpora
├────────────────────────┤
│ Phase 4: Hardware & OS │ ──► libgpiod v2 + sysfs fallback, systemd sd_notify watchdog,
│                        │     multi-sink logging, telemetry JSON schema & validator
├────────────────────────┤
│ Phase 5: Security      │ ──► MQTT 5.0 + mTLS, IKeyStore abstraction, BLE LESC security,
│                        │     FOTA signature check, input validation tests, SECURITY.md, SBOM
├────────────────────────┤
│ Phase 6: Docs & Clean  │ ──► Doxygen, thread-safety rules, ErrorCode catalog, LICENSE,
│                        │     SPDX headers, CHANGELOG.md, CONTRIBUTING.md, MIGRATION.md, README
└────────────────────────┘
```

---
*Standards Audit completed. Awaiting user approval to proceed with Phase 1.*
