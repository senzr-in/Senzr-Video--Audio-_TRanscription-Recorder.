# Rockchip RK3588 / RK3588S AI Inference & Hardware Performance Qualification Specification

## 1. Target Hardware

| Parameter        | Target                        |
| ---------------- | ----------------------------- |
| SoC              | Rockchip RK3588 / RK3588S     |
| Process          | 8nm LP                        |
| CPU              | 4× Cortex-A76 + 4× Cortex-A55 |
| NPU              | Triple-core RKNN NPU          |
| NPU Peak Compute | 6.0 TOPS INT8                 |
| RAM              | 16 GB LPDDR4x                 |
| Architecture     | aarch64                       |
| Target OS        | Senzr OS / Linux              |
| SDK              | `libsenzr`                    |

> [!IMPORTANT]
> Exact CPU frequencies, RAM configuration, kernel version, NPU driver version, and cooling configuration must be recorded from the physical test system rather than assumed from the board profile.

---

## 2. Evidence Classification

Every performance result MUST have an evidence classification:

### `PHYSICAL_MEASURED`
Measured on an actual physical RK3588/RK3588S board using the production SDK and hardware.

**Required evidence:**
* Physical board identification
* Model & revision
* OS & kernel version
* SDK version
* RKNN runtime / RKNPU driver version
* Test configuration
* Raw benchmark log
* Temperature measurements
* Accelerator utilization
* Latency measurements (std::chrono::steady_clock)

### `PROFILED`
Generated using RKNN-Toolkit2/RKNN profiler or equivalent vendor tooling without execution of the production workload on the target physical board.

### `SIMULATED`
Generated using the SDK host mock/emulation layer.

### `REFERENCE`
Theoretical/vendor/reference specification.

### `HOST_VALIDATED`
Verified on a development Linux host without the target hardware accelerator.

### `NOT_TESTED`
No valid test evidence exists.

---

## 3. Current Performance Baseline

The following values are the current SDK development baseline:

| Model / Workload       | Framework       | Precision | Core Latency | Throughput | NPU Utilization |      Thermal Rise | Evidence         |
| ---------------------- | --------------- | --------: | -----------: | ---------: | --------------: | ----------------: | ---------------- |
| YOLOv8s Vehicle/Object | RKNN            |      INT8 |       3.2 ms |   180+ FPS |             68% |  +8.2°C estimated | `PROFILED`       |
| YOLOv8n Pose           | RKNN            |      INT8 |       2.1 ms |   240+ FPS |             52% |  +5.4°C estimated | `PROFILED`       |
| PP-OCRv4 Recognition   | RKNN            |      INT8 |       1.8 ms |   310+ FPS |             45% |  +3.8°C estimated | `PROFILED`       |
| Dual Vision + OCR      | RKNN Concurrent |      INT8 |       4.9 ms |   120+ FPS |             92% | +12.5°C estimated | `PROFILED`       |
| Host CPU Fallback      | `libsenzr` Core |      FP32 |      18.0 ms |     55 FPS |             N/A |               N/A | `HOST_VALIDATED` |

> [!WARNING]
> These values MUST NOT be described as physical RK3588/RK3588S benchmark results until physical measurements are available on genuine hardware.

---

## 4. Physical Hardware Benchmark Requirement

To classify a result as `PHYSICAL_MEASURED`, the benchmark MUST execute on the physical target board.

**Recommended initial qualification boards:**
* Orange Pi 5 Max — RK3588
* Radxa Rock 5B — RK3588

Additional RK3588/RK3588S boards may be qualified using the same procedure.

---

## 5. Test Environment

Record the following before every benchmark:

```text
Board:
Board revision:
SoC:
RAM:
Storage:
OS:
Kernel:
SDK version:
Application version:
RKNN runtime version:
RKNPU driver version:
Model:
Model version:
Model input resolution:
Precision:
Cooling:
Fan configuration:
Ambient temperature:
Power supply:
Power mode:
CPU governor:
NPU configuration:
```

Do not assume these values from the platform profile. Read and record them from the actual system.

---

## 6. Required Warm-Up

Before collecting performance measurements:
* **Warm-up iterations**: 10 minimum

Warm-up results MUST NOT be included in the final latency statistics.

The purpose is to allow:
* Runtime initialization
* Model loading
* Memory allocation
* NPU initialization
* Cache stabilization
* Thermal stabilization

---

## 7. Measurement Run

Execute:
* **1,000 continuous inference iterations** using the production `libsenzr` inference API.

Use:
```cpp
std::chrono::steady_clock
```
for application-level latency timing. Do not use wall-clock time for latency measurements.

Record every inference duration. Calculate:
* Minimum
* Maximum
* Mean
* P50
* P90
* P95
* P99

Also calculate:
```text
FPS = 1000 / average_latency_ms
```
where appropriate for a single sequential pipeline.

For concurrent pipelines, calculate throughput from completed frames/events over the measured interval rather than deriving it only from single-inference latency.

---

## 8. NPU Monitoring

During the physical benchmark, record NPU utilization using the target system's available RKNPU telemetry interface.

Where available:
```text
/sys/kernel/debug/rknpu/load
```

Record:
* Average NPU utilization
* Peak NPU utilization
* Per-core utilization where available

If the telemetry interface is unavailable on a particular OS/driver combination, record:
```text
NPU utilization: NOT_AVAILABLE
```

Do not invent utilization values.

---

## 9. Thermal Monitoring

Monitor SoC temperature during the complete benchmark.

Where applicable:
```text
/sys/class/thermal/thermal_zone*/temp
```

Identify which thermal zone corresponds to the SoC/CPU/NPU before collecting results.

Record:
* Idle temperature
* Start temperature
* Peak temperature
* End temperature
* Ambient temperature
* Temperature rise
* Thermal throttling

Thermal rise MUST be calculated from actual measurements:
```text
Thermal Rise = Peak Test Temperature - Baseline Temperature
```

Do not use estimated thermal rise values in a `PHYSICAL_MEASURED` result.

---

## 10. Power Measurement

For final hardware qualification, measure board power where practical.

Record:
* Idle power
* Inference average power
* Inference peak power
* Voltage
* Current

Use an external power meter or an appropriately documented hardware measurement method. Do not infer board power from software alone.

---

## 11. Single-Model Qualification

Run each workload independently:

### Test A
* Workload: YOLOv8s Vehicle/Object
* Precision: INT8

### Test B
* Workload: YOLOv8n Pose
* Precision: INT8

### Test C
* Workload: PP-OCRv4 Recognition
* Precision: INT8

For each model collect:
* 1,000 measured iterations
* Latency statistics (Min, Mean, P50, P90, P95, P99, Max)
* FPS (throughput)
* NPU utilization
* Temperature (Idle, Start, Peak, End, Rise)
* Power
* Memory
* CPU utilization

---

## 12. Concurrent Pipeline Qualification

Run **Vision + OCR** concurrently through the production SDK.

Verify:
* Model initialization
* Concurrent inference
* NPU scheduling
* Queue behavior
* Latency
* Throughput
* Memory stability
* Thermal behavior
* No dropped work
* No deadlocks
* No crashes

Run at least **1,000 completed pipeline operations**.

Record:
* P50, P95, P99 latency
* Average throughput
* Peak NPU utilization
* Peak temperature
* Memory usage
* Dropped operations
* Failed operations

---

## 13. Long-Duration AI Stability Test

After single-run benchmarking, perform **1 hour** continuous AI inference.

For final release qualification:
* **6 hours**
* **24 hours** (where required by the hardware qualification plan)

Monitor:
* RSS
* System RAM
* CPU utilization
* NPU utilization
* Temperature
* Thermal throttling
* Inference latency
* FPS
* Failed inference operations
* Process crashes
* Kernel errors

The benchmark must report whether performance degraded over time.

---

## 14. Memory Stability

Capture:
* Initial RSS
* After 1,000 iterations
* After 10,000 iterations
* After 100,000 iterations
* End RSS

Calculate:
```text
Memory Delta = End RSS - Initial RSS
```

Also monitor system memory. A constant process RSS alone is not sufficient to prove complete system-level memory stability.

---

## 15. Thermal Throttling

The test MUST detect thermal throttling.

If throttling occurs:
```text
Thermal Status: FAIL / INVESTIGATION REQUIRED
```

Record:
* Temperature at throttling
* Observed frequency
* NPU behavior
* Performance before throttling
* Performance after throttling
* Cooling configuration
* Ambient temperature

Do not hide throttling by reporting only the best-performing iterations.

---

## 16. Reproducibility

Repeat the benchmark at least **3 independent runs** for final qualification.

Each run must contain:
* 10 warm-up iterations
* 1,000 measurement iterations

Report:
* Run 1
* Run 2
* Run 3
* Overall mean
* Best
* Worst
* Variance where useful

A single successful run is not sufficient for final hardware qualification.

---

## 17. Physical Evidence Package

For every physical benchmark generate:
```text
docs/test-results/rk3588/<board>/<date>/
```

containing:
* `system-info.txt`
* `benchmark.log`
* `latency.csv`
* `thermal.log`
* `npu-load.log`
* `power.log`
* `memory.log`
* `kernel.log`
* `benchmark-summary.md`

If available also store:
* `oscilloscope/`
* `photos/`
* `serial-console/`

Do not fabricate any of these files.

---

## 18. Benchmark Result Format

Every physical result must use:

```yaml
Board: "Orange Pi 5 Max"
Board Revision: "v1.1"
SoC: "Rockchip RK3588"
RAM: "16 GB LPDDR4x"
OS: "Senzr OS / Linux 5.10.160-rockchip"
Kernel: "5.10.160-rockchip"
SDK: "libsenzr 1.0.0"
RKNN Runtime: "v1.6.0"
RKNPU Driver: "0.8.8"
Model: "YOLOv8s"
Precision: "INT8"
Input: "640x640x3"
Cooling: "Active Heatsink + PWM Fan"
Ambient: "25.0°C"

Warm-up: 10
Measurement iterations: 1000

Latency:
  Min: 2.8 ms
  Mean: 3.2 ms
  P50: 3.1 ms
  P95: 3.6 ms
  P99: 4.1 ms
  Max: 5.2 ms

Throughput:
  FPS: 185

NPU:
  Average: 65%
  Peak: 72%

Temperature:
  Idle: 38.2°C
  Start: 39.0°C
  Peak: 47.2°C
  End: 46.5°C
  Rise: +8.2°C

Power:
  Idle: 2.8 W
  Average: 6.2 W
  Peak: 8.5 W

Memory:
  Start RSS: 142 MB
  End RSS: 144 MB
  Delta: +2 MB

Thermal throttling: None
Crashes: 0
Failed inferences: 0
Dropped frames: 0

Evidence: PHYSICAL_MEASURED
```

---

## 19. Qualification Rules

* A benchmark can be promoted from `PROFILED` to `PHYSICAL_MEASURED` only when it has actual physical-board evidence.
* A board can be marked `HARDWARE_VALIDATED` only after the required hardware qualification tests pass.
* Do not convert `PROFILED` → `PHYSICAL_MEASURED` without physical execution.
* Do not convert `HOST_VALIDATED` → `HARDWARE_VALIDATED` without hardware evidence.

---

## 20. Final Performance Report

The final report must clearly separate:

### Development Baseline
* `PROFILED` / `HOST_VALIDATED` / `REFERENCE`

### Physical Benchmark
* `PHYSICAL_MEASURED`

### Hardware Qualification
* `PHYSICAL_PASS` / `PHYSICAL_FAIL`

The existing projected values may remain as engineering reference values, but they must not be presented as certified physical performance.

---

## 21. Release Requirement

The Senzr SDK must remain:
```text
HOST_VALIDATED
```
until the physical RK3588/RK3588S qualification evidence is complete.

Only after the required physical tests pass may the SDK progress toward:
```
HOST_VALIDATED
      ↓
HARDWARE_VALIDATED
      ↓
  QUALIFIED
      ↓
RELEASE_READY
```

The goal is not to make the benchmark numbers look better; the goal is to produce **reproducible, auditable, physically verified performance evidence** for the Senzr SDK.
