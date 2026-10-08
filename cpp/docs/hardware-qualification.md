# Hardware Qualification Matrix & Physical Evidence Specification

## Qualification Status Hierarchy

Every target platform and board model is evaluated against the following strict evidence tiers:

* `NOT_TESTED`: No validation performed on this hardware subsystem.
* `SIMULATED`: Validated solely via software stubs or simulated runtime responses.
* `HOST_VALIDATED`: Full compilation, CTest suite, and external consumer packaging verified on development host.
* `PROFILED`: Derived from vendor compiler profiler traces (e.g. RKNN-Toolkit / TensorRT trtexec).
* `PHYSICAL_MEASURED`: Benchmark measurements collected via hardware timers and sensors on real physical silicon.
* `PHYSICAL_PASS`: Physical bench qualification passed with zero regressions.
* `PHYSICAL_FAIL`: Physical bench qualification failed.

---

## Target Board Qualification Matrix

| Board Model | SDK Core | GPIO | Watchdog | Camera | AI | MQTT/TLS | FOTA | 24h Soak | Overall Qualification |
|---|---|---|---|---|---|---|---|---|---|
| **Orange Pi 5 Max** | `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | `NOT_TESTED` | `PROFILED` | `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | **`HOST_VALIDATED`** |
| **Radxa Rock 5B** | `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | `NOT_TESTED` | `PROFILED` | `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | **`HOST_VALIDATED`** |
| **FriendlyElec NanoPC-T6**| `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | `NOT_TESTED` | `PROFILED` | `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | **`HOST_VALIDATED`** |
| **KHADAS Edge2** | `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | `NOT_TESTED` | `PROFILED` | `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | **`HOST_VALIDATED`** |
| **NVIDIA Jetson Nano 4GB**| `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | `NOT_TESTED` | `PROFILED` | `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | **`HOST_VALIDATED`** |
| **NVIDIA Jetson Nano 2GB**| `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | `NOT_TESTED` | `PROFILED` | `HOST_VALIDATED` | `NOT_TESTED` | `NOT_TESTED` | **`HOST_VALIDATED`** |

> [!IMPORTANT]
> A hardware platform can only progress from `HOST_VALIDATED` to `HARDWARE_VALIDATED` once physical electrical testing (oscilloscope GPIO, hardware watchdog reboot, physical camera streaming, live NPU/GPU inference) has been performed and logged in `docs/test-results/`.

---

## Physical Qualification Requirements per Subsystem

### 1. Physical GPIO Qualification
* **Relay Output (Pin 7)**: Verify 3.3V logic high output on pulse via oscilloscope; measure pulse rise/fall times (<50μs).
* **Buzzer Output (Pin 11)**: Verify frequency generation and pulse duration across 100 consecutive activations.
* **Safety Beam Input (Pin 15)**: Verify active LOW transition triggers door relay disarm within <2ms measured via dual-channel oscilloscope.

### 2. Hardware Watchdog Qualification
* **Hard Reset Verification**: Stop feeding `/dev/watchdog` and confirm physical SoC hard reset occurs within the hardware timeout window (15s).
* **Boot Recovery**: Verify system boots cleanly into operational state with zero database or filesystem corruption.

### 3. Physical FOTA Qualification
* **Power-Loss Inoculation**: Physically disconnect power during active slot writing and verify bootloader boots previous intact slot without bricking.
* **Cryptographic Rejection**: Verify modified byte in firmware package triggers SHA-256 validation failure before flashing.

### 4. 24-Hour Physical Soak Test
* **Continuous Load**: Run camera frame ingest + AI ANPR inference + SQLite storage + MQTT heartbeat for 24 continuous hours.
* **Limits**: Zero crashes, zero memory growth (<5MB RSS delta), zero queue leaks, zero thread leaks.
