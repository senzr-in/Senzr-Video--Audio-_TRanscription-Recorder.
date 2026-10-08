# NVIDIA Jetson Nano AI Inference & Hardware Performance Qualification Specification

## 1. Target Hardware

| Parameter        | Target                        |
| ---------------- | ----------------------------- |
| SoC              | NVIDIA Tegra X1 / T210        |
| Process          | 20nm                          |
| CPU              | 4× ARM Cortex-A57 MPCore      |
| GPU              | 128-Core NVIDIA Maxwell GPU   |
| GPU Peak Compute | 472 GFLOPS FP16               |
| RAM              | 4 GB 64-bit LPDDR4 (Unified)  |
| Architecture     | aarch64                       |
| Target OS        | Linux for Tegra / JetPack 4.6 |
| SDK              | `libsenzr`                    |

> [!IMPORTANT]
> Exact CPU/GPU frequencies, RAM configuration, kernel version, L4T/JetPack version, power mode (`nvpmodel`), and cooling configuration must be recorded from the physical test system rather than assumed from the board profile.

---

## 2. Evidence Classification

Every performance result MUST have an evidence classification:

### `PHYSICAL_MEASURED`
Measured on an actual physical Jetson Nano board using the production SDK and hardware.

**Required evidence:**
* Physical board identification & revision (e.g. B01)
* JetPack / L4T version
* OS & kernel version
* SDK version
* CUDA / TensorRT / cuDNN runtime versions
* Power mode (`nvpmodel -m 0` / `jetson_clocks`)
* Raw benchmark log
* Temperature measurements (`tegrastats` / sysfs)
* GPU / CPU utilization
* Latency measurements (`std::chrono::steady_clock`)

### `PROFILED`
Generated using NVIDIA TensorRT / `trtexec` profiler or equivalent vendor tooling without execution of the production workload on the target physical board.

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

| Model / Workload       | Framework       | Precision | Core Latency | Throughput | GPU Utilization |      Thermal Rise | Evidence         |
| ---------------------- | --------------- | --------: | -----------: | ---------: | --------------: | ----------------: | ---------------- |
| YOLOv8n Vehicle/Object | TensorRT 8.2    |      FP16 |       8.5 ms |     45 FPS |             88% | +11.4°C estimated | `PROFILED`       |
| YOLOv8s Vehicle/Object | TensorRT 8.2    |      FP16 |      16.2 ms |     28 FPS |             96% | +14.2°C estimated | `PROFILED`       |
| PP-OCRv4 Recognition   | TensorRT 8.2    |      FP16 |       6.4 ms |     85 FPS |             72% |  +8.1°C estimated | `PROFILED`       |
| MobileNetV2 Classifier | TensorRT 8.2    |      FP16 |       3.8 ms |   150+ FPS |             65% |  +5.6°C estimated | `PROFILED`       |
| Host CPU Fallback      | `libsenzr` Core |      FP32 |      18.0 ms |     55 FPS |             N/A |               N/A | `HOST_VALIDATED` |

> [!WARNING]
> These values MUST NOT be described as physical Jetson Nano benchmark results until physical measurements are available on genuine hardware.

---

## 4. Physical Hardware Benchmark Requirement

To classify a result as `PHYSICAL_MEASURED`, the benchmark MUST execute on the physical target board.

**Recommended initial qualification boards:**
* NVIDIA Jetson Nano 4GB Developer Kit (B01)
* NVIDIA Jetson Nano 2GB Developer Kit

Additional Jetson modules may be qualified using the same procedure.

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
JetPack version:
L4T version:
CUDA version:
TensorRT version:
SDK version:
Application version:
Model:
Model version:
Model input resolution:
Precision:
Cooling:
Fan configuration:
Ambient temperature:
Power supply (5V/4A DC jack or Micro-USB):
Power mode (nvpmodel -m 0 / 10W MAXN):
CPU/GPU clocks (jetson_clocks state):
```

Do not assume these values from the platform profile. Read and record them from the actual system.

---

## 6. Required Warm-Up

Before collecting performance measurements:
* **Warm-up iterations**: 10 minimum

Warm-up results MUST NOT be included in the final latency statistics.

The purpose is to allow:
* TensorRT engine deserialization / runtime initialization
* CUDA memory allocation and page table locking
* GPU clock stabilization
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

## 8. GPU / Accelerator Monitoring

During the physical benchmark, record GPU and CPU utilization using `tegrastats`.

Record:
* Average GPU utilization (`GR3D_FREQ` %)
* Peak GPU utilization
* Shared Unified RAM usage (RAM consumed / 4096MB)
* EMC (Memory Controller) frequency & load

If telemetry is unavailable, record:
```text
GPU utilization: NOT_AVAILABLE
```

Do not invent utilization values.

---

## 9. Thermal Monitoring

Monitor SoC temperature during the complete benchmark.

Where applicable:
```text
/sys/devices/virtual/thermal/thermal_zone1/temp (GPU)
/sys/devices/virtual/thermal/thermal_zone0/temp (CPU)
```

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

For final hardware qualification, measure board power via onboard INA3221 telemetry (`/sys/bus/i2c/drivers/ina3221x/`) or external power meter.

Record:
* Idle power (W)
* Inference average power (W)
* Inference peak power (W)
* VDD_IN voltage and current

---

## 11. Single-Model Qualification

Run each workload independently:

### Test A
* Workload: YOLOv8n Vehicle/Object
* Precision: FP16

### Test B
* Workload: YOLOv8s Vehicle/Object
* Precision: FP16

### Test C
* Workload: PP-OCRv4 Recognition
* Precision: FP16

For each model collect:
* 1,000 measured iterations
* Latency statistics (Min, Mean, P50, P90, P95, P99, Max)
* FPS (throughput)
* GPU utilization
* Temperature (Idle, Start, Peak, End, Rise)
* Power
* Memory (Unified RAM)
* CPU utilization

---

## 12. Concurrent Pipeline Qualification

Run **Vision + OCR** concurrently through the production SDK.

Verify:
* Engine initialization
* Concurrent CUDA stream scheduling
* Latency & throughput
* Unified memory stability (no OOM killer triggers)
* Thermal behavior
* No dropped work / deadlocks

Run at least **1,000 completed pipeline operations**.

---

## 13. Long-Duration AI Stability Test

After single-run benchmarking, perform **1 hour** continuous AI inference.

For final release qualification:
* **6 hours**
* **24 hours** (where required by the hardware qualification plan)

Monitor:
* RSS & unified system memory
* CPU / GPU utilization
* Temperature & throttling
* Inference latency stability
* Zero process crashes / CUDA kernel errors

---

## 14. Memory Stability

Capture:
* Initial RSS & Unified RAM
* After 1,000 iterations
* After 10,000 iterations
* After 100,000 iterations
* End RSS

Calculate:
```text
Memory Delta = End RSS - Initial RSS
```

---

## 15. Thermal Throttling

The test MUST detect thermal throttling.

If throttling occurs:
```text
Thermal Status: FAIL / INVESTIGATION REQUIRED
```

Record:
* Temperature at throttling
* Observed GPU/CPU clock reduction
* Performance before and after throttling

---

## 16. Reproducibility

Repeat the benchmark at least **3 independent runs** for final qualification.

Report:
* Run 1
* Run 2
* Run 3
* Overall mean, best, worst, variance

---

## 17. Physical Evidence Package

For every physical benchmark generate:
```text
docs/test-results/jetson/<board>/<date>/
```

containing:
* `system-info.txt`
* `benchmark.log`
* `latency.csv`
* `thermal.log`
* `tegrastats.log`
* `power.log`
* `memory.log`
* `kernel.log`
* `benchmark-summary.md`

---

## 18. Benchmark Result Format

Every physical result must use:

```yaml
Board: "NVIDIA Jetson Nano Developer Kit"
Board Revision: "B01"
SoC: "NVIDIA Tegra X1 (T210)"
RAM: "4 GB 64-bit LPDDR4"
OS: "Ubuntu 18.04 LTS (L4T R32.7.3)"
Kernel: "4.9.253-tegra"
SDK: "libsenzr 1.0.0"
JetPack: "4.6.3"
CUDA: "10.2.300"
TensorRT: "8.2.1.8"
Model: "YOLOv8n"
Precision: "FP16"
Input: "640x640x3"
Cooling: "Passive Heatsink + 5V Noctua Fan"
Ambient: "25.0°C"
Power Mode: "MAXN (10W)"

Warm-up: 10
Measurement iterations: 1000

Latency:
  Min: 8.1 ms
  Mean: 8.5 ms
  P50: 8.4 ms
  P95: 9.1 ms
  P99: 9.8 ms
  Max: 11.2 ms

Throughput:
  FPS: 45

GPU:
  Average: 88%
  Peak: 95%

Temperature:
  Idle: 35.0°C
  Start: 36.5°C
  Peak: 47.9°C
  End: 47.1°C
  Rise: +11.4°C

Power:
  Idle: 2.1 W
  Average: 7.8 W
  Peak: 9.6 W

Memory:
  Start Unified RAM: 1.2 GB
  End Unified RAM: 1.2 GB
  Delta: 0 MB

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

---

## 20. Final Performance Report

The final report must clearly separate:
* Development Baseline: `PROFILED` / `HOST_VALIDATED` / `REFERENCE`
* Physical Benchmark: `PHYSICAL_MEASURED`
* Hardware Qualification: `PHYSICAL_PASS` / `PHYSICAL_FAIL`

---

## 21. Release Requirement

The Senzr SDK must remain:
```text
HOST_VALIDATED
```
until physical Jetson Nano qualification evidence is complete.
