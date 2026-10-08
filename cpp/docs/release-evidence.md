# Senzr SDK Release Evidence Audit & Hardware Qualification Gate

## Executive Summary

```text
Senzr SDK Release Evidence
==========================

Current Status:
HOST_VALIDATED

Physical Hardware:
PENDING

Release Decision:
NOT_RELEASE_READY (Awaiting physical hardware bench qualification)
```

> [!IMPORTANT]
> The Senzr OS C++ SDK (`libsenzr`) release status is officially **`HOST_VALIDATED`**. It must NOT be promoted to `HARDWARE_VALIDATED`, `QUALIFIED`, or `RELEASE_READY` until physical hardware bench evidence packages are collected from physical silicon and logged in `docs/test-results/`.

---

## Subsystem Evidence Audit Breakdown

### 1. Host Tests (CTest Suite)
* **Status**: **`PASS`** (`HOST_VALIDATED`)
* **Evidence**: 9/9 CTest unit and integration tests passing on Linux development host (`host-ctest-report.txt`):
  * `public_headers`: PASS (0.00s) — Independent compilation of all 16 public SDK headers.
  * `queues`: PASS (0.29s) — ThreadSafeQueue concurrency under multi-threaded contention.
  * `config`: PASS (0.01s) — JSON schema parsing, serialization, and validation.
  * `session`: PASS (0.01s) — Session lifecycle, video segment tracking, metadata generation.
  * `updater`: PASS (0.01s) — FOTA state machine and version fallback logic.
  * `plate_utils`: PASS (0.14s) — Indian/Bharat ANPR string normalization & checksum logic.
  * `platform`: PASS (0.01s) — Dynamic platform discovery, capability querying, and tensor execution.
  * `unsupported`: PASS (0.01s) — Missing capabilities cleanly return `ErrorCode::NotSupported`.
  * `soak`: PASS (0.10s) — Multi-iteration session soak with zero memory growth.

### 2. Physical Hardware Benchmarks
* **Status**: **`PENDING / NOT_TESTED`**
* **Target Hardware Scope**:
  * Rockchip RK3588 (Orange Pi 5 Max, Radxa Rock 5B, FriendlyElec NanoPC-T6)
  * NVIDIA Jetson Nano 4GB/2GB (Tegra X1 / T210)
* **Blocker**: Physical boards are awaiting lab bench instrumentation (oscilloscopes, power analyzers, thermocouples).

### 3. AI Subsystem
* **Software HAL**: `HOST_VALIDATED` — Unified `InferenceEngine` & `Tensor` abstractions with dynamic runtime routing (`Rknn`, `TensorRT`, `Cpu`).
* **Offline Compiler Baselines**: `PROFILED` — RKNN-Toolkit2 (3.2 ms INT8) & TensorRT (8.5 ms FP16).
* **Physical Hardware Latency**: `NOT_TESTED` — Physical NPU/GPU steady-clock benchmarks pending on live hardware.
* **Physical NPU/GPU Telemetry**: `NOT_TESTED` — Live `/sys/kernel/debug/rknpu/load` & `tegrastats` logging pending.

### 4. Camera Subsystem
* **Software HAL**: `HOST_VALIDATED` — Dynamic V4L2 device discovery (`/dev/video*`) and CSI bus enumeration.
* **Missing Camera Handling**: `HOST_VALIDATED` — Returns `ErrorCode::ResourceUnavailable` truthfully when no physical video node is present; zero fake frames.
* **Physical Camera Streaming**: `NOT_TESTED` — Continuous 1080p/4K MIPI CSI-2 and USB V4L2 ingest pending physical bench.

### 5. GPIO Subsystem & Safety Interlock
* **Logical Mapping**: `HOST_VALIDATED` — RK3588 (40-pin) & Jetson Nano (J41) logical-to-sysfs pin mapping implemented.
* **Software Interlock**: `HOST_VALIDATED` — Non-blocking `std::jthread` pulse timing & emergency disarm logic verified.
* **Physical Electrical Validation**: `NOT_TESTED` — 3.3V logic high voltage levels, rise/fall edge times (<50μs), and <2ms safety beam cut-off pending 2-channel oscilloscope testing.

### 6. Watchdog Subsystem
* **Software Supervisor**: `HOST_VALIDATED` — Background health-check loop and software watchdog supervisor verified.
* **Hardware Watchdog Hard Reset**: `NOT_TESTED` — Physical `/dev/watchdog` starvation, SoC reset, and cold boot recovery pending serial UART bench testing.

### 7. FOTA (Firmware Over-The-Air) Subsystem
* **State Machine & Integrity**: `HOST_VALIDATED` — SHA-256 cryptographic verification, dual-slot state transitions, and rollback error handling verified.
* **Physical Power-Cut Recovery**: `NOT_TESTED` — Physical power cut during active flash write to verify bootloader A/B slot fallback pending relay-driven power cycle bench.

### 8. MQTT / TLS Subsystem
* **Client Architecture**: `HOST_VALIDATED` — Thread-safe `Client` with auto-reconnection and JSON payload dispatch.
* **Security Default**: Hardened to port **8883 (MQTTS)** with mandatory TLS 1.3 CA certificate verification.
* **Live Network Validation**: `NOT_TESTED` — Physical broker disconnection, TLS handshake failure, and offline queue recovery pending network lab test.

### 9. Storage Subsystem (SQLite / SQLCipher)
* **API Layer**: `HOST_VALIDATED` — Crash-resilient SQLite WAL configuration and atomic transaction rollbacks.
* **Physical Failure Scenarios**: `NOT_TESTED` — Disk-full simulation and sudden ungraceful shutdown during high-frequency writes pending bench testing.

### 10. Security & Secret Audit
* **Audit Result**: **`PASS`** (`HOST_VALIDATED`)
* **Findings**:
  * Repository-wide audit for `password`, `secret`, `token`, `api_key`, `private_key`, `1883`, `mqtt://` completed.
  * Zero hardcoded production credentials, default passwords, or plaintext keys in source files.
  * All external cloud credentials sourced exclusively from environment variables (`AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`).
  * Default insecure MQTT (port 1883) replaced with secure MQTTS (port 8883 / TLS 1.3).

### 11. Memory Stability
* **Automated Host Soak**: **`PASS`** (`HOST_VALIDATED`) — 200/1000 continuous session cycles executed with constant process RSS (2,860 KB; delta: 0 KB).
* **24-Hour Physical Soak**: **`NOT_TESTED`** — Multi-subsystem 24-hour continuous load (Camera + AI + SQLite + MQTT) pending physical bench.

### 12. API & ABI Packaging
* **Header Isolation**: **`PASS`** (`HOST_VALIDATED`) — Zero internal `app/*` or vendor headers leaked in public `include/senzr/`.
* **CMake Packaging**: **`PASS`** (`HOST_VALIDATED`) — Clean install verified; external consumer project links `find_package(Senzr REQUIRED)` and `Senzr::SDK` cleanly.

---

## Open Blockers for `RELEASE_READY`

| Blocker ID | Subsystem | Description | Required Resolution |
|---|---|---|---|
| **BLK-001** | Physical AI Benchmark | RK3588 NPU & Jetson Nano GPU physical inference unmeasured | Execute 1,000-iteration steady-clock benchmark on physical boards; log to `docs/test-results/` |
| **BLK-002** | Physical GPIO / Safety | Electrical verification of 3.3V levels and <2ms interlock response unmeasured | Capture oscilloscope traces for relay pulse & beam break |
| **BLK-003** | Hardware Watchdog | Physical `/dev/watchdog` hardware reset unverified | Perform UART-logged starvation test on physical silicon |
| **BLK-004** | Physical FOTA | Power-cut resilience during active flash write unverified | Perform automated power-cut test during slot update |
| **BLK-005** | 24-Hour Soak | 24-hour continuous multi-subsystem stress run unexecuted | Run 24h continuous test under active camera & AI load |

---

## Release Recommendation

* **Current Stage**: **`HOST_VALIDATED`**
* **Next Progression Gate**: **`HARDWARE_VALIDATED`** (Requires resolving BLK-001 through BLK-005)
* **Release Decision**: **`NOT_RELEASE_READY`**
