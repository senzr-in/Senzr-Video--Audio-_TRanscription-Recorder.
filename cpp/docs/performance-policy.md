# Senzr OS C++ SDK (`libsenzr`) Performance & Evidence Policy

## Core Principle

> **Senzr does not publish theoretical, simulated, or profiler-derived performance as guaranteed production performance.**

Performance documentation and benchmark reports must reflect what customers actually experience on physical edge hardware in production environments.

---

## Real-World Performance Philosophy

Edge AI and embedded system throughput is never a single static number. End-to-end performance depends on a multi-stage hardware and software pipeline:

```
Camera Sensor / V4L2 Ingest
           ↓
   Frame Preprocessing (Resize, Color Space Conversion, Letterboxing)
           ↓
   Host-to-Device Memory Transfer (Unified RAM / NPU DDR)
           ↓
   Hardware Inference Core (RKNPU / TensorRT Maxwell GPU)
           ↓
   Postprocessing (NMS, Thresholding, Bounding Box Decoding)
           ↓
   Application Logic (Access Control, Tracking, OCR Correction)
           ↓
   Storage & Cloud Telemetry (SQLite WAL, MQTTS Event Dispatch)
```

Quoting raw NPU/GPU core execution time in isolation (e.g. "3.2 ms") does not guarantee that a complete production application will achieve the corresponding theoretical frame rate (e.g. "313 FPS"). 

Senzr documentation and reports strictly distinguish between:
1. **Core Model Inference Latency**: Isolated execution time within the neural accelerator.
2. **End-to-End Application Latency**: Complete wall-clock latency from sensor capture to application action.

---

## Real-World Performance Variables

Customer-measured performance is subject to real-world variables, including:

* **Hardware Revision & Thermal Design**: Board revision, passive vs. active heatsink, fan CFM, thermal pad conductivity.
* **Ambient Conditions**: Enclosure ambient temperature, airflow restrictions, sun exposure.
* **SoC Operating Points**: Dynamic voltage/frequency scaling (DVFS), CPU governor (`performance` vs `schedutil`), GPU/NPU clock levels (`jetson_clocks`).
* **Kernel & Driver Stack**: Vendor BSP kernel version, RKNPU kernel driver revision, L4T JetPack release, CUDA/cuDNN versions.
* **Model Parameters**: Quantization precision (INT8 vs FP16 vs FP32), input tensor resolution, batch size, sparsity.
* **System Concurrency**: Background tasks, network I/O, disk write bandwidth (microSD vs eMMC vs NVMe), concurrent video streams.

---

## Evidence Classification Hierarchy

Every metric published by Senzr is tagged with an immutable evidence classification:

| Evidence Class | Definition | Acceptable Source | Customer Guarantee |
|---|---|---|---|
| `REFERENCE` | Theoretical maximum or architectural limit | Silicon vendor datasheets (e.g. 6.0 TOPS, 472 GFLOPS) | Theoretical upper ceiling only |
| `SIMULATED` | Generated via host mock/emulation layer | SDK unit test environment stubs | Software logic verification only |
| `PROFILED` | Derived from vendor toolchain compilers | RKNN-Toolkit2 / TensorRT `trtexec` profiler logs | Optimization guidance only |
| `HOST_VALIDATED` | Executed and validated on Linux development host | CTest suite, external CMake packaging test | API & cross-platform logic verified |
| `PHYSICAL_MEASURED` | Executed on physical target silicon with calibrated instruments | Production `libsenzr` running on physical board, `steady_clock`, hardware telemetry | Physically verified engineering benchmark |
| `HARDWARE_VALIDATED` | Physical platform passed all mandatory hardware qualification gates | Physical GPIO, watchdog reset, camera streaming, 24h soak logs | Subsystem qualified on target board |
| `QUALIFIED` | All platform subsystems meet production acceptance criteria | Full physical test suite execution without regressions | Qualified for target hardware deployment |
| `RELEASE_READY` | Commercial release gate passed across all target hardware tiers | Zero open blockers, signed reproducible artifacts | Commercial production ready |
| `NOT_TESTED` | No physical validation has been performed | Unverified subsystem or untested board variant | No performance claim made |
| `BLOCKED` | Testing is pending prerequisite hardware, tooling, or drivers | Missing physical lab equipment or pending vendor fix | Test blocked |
| `FAILED` | Physical testing did not meet qualification threshold | Test execution resulted in crash, leak, or regression | Investigation required |

---

## Customer-Facing Performance Reporting Standard

Customer-facing technical reports must prioritize truthfulness and reproducibility:

| Workload / Subsystem Metric | Status / Evidence Class | Notes |
|---|---|---|
| **NPU Peak Compute (RK3588)** | `REFERENCE` (6.0 TOPS INT8) | Vendor theoretical maximum |
| **GPU Peak Compute (Jetson Nano)** | `REFERENCE` (472 GFLOPS FP16) | Vendor theoretical maximum |
| **Model Compiler Profiler Latency** | `PROFILED` | Vendor offline toolchain profile |
| **Physical Hardware Inference Latency** | `NOT_TESTED` (Physical Bench Pending) | Will be updated upon physical bench execution |
| **End-to-End Camera-to-Action Latency** | `NOT_TESTED` (Physical Bench Pending) | Requires physical camera & sensor bench |
| **Sustained Production FPS** | `NOT_TESTED` (Physical Bench Pending) | Requires physical bench execution |
| **Thermal Rise Under Load** | `NOT_TESTED` (Physical Bench Pending) | Requires calibrated thermocouple measurement |
| **System Power Consumption** | `NOT_TESTED` (Physical Bench Pending) | Requires external power meter verification |

---

## Release Gate Rule

The Senzr SDK release status remains strictly **`HOST_VALIDATED`** until physical benchmark and hardware qualification evidence packages (`docs/test-results/<platform>/<board>/<date>/`) have been produced and verified on genuine hardware.
