# Release & Qualification Distribution Process

## Qualification Status Hierarchy

Every build, target board, and release artifact of `libsenzr` is evaluated against a 5-tier qualification standard:

1. `DEVELOPMENT`: Active driver, platform HAL, or architectural work in progress.
2. `HOST_VALIDATED`: Clean standalone header build, mock tests, CTest, and external CMake packaging pass on development host / simulation.
3. `HARDWARE_VALIDATED`: Board boots physically, GPIO outputs switch verified by oscilloscope, camera streams frames, and accelerator loads model on real silicon.
4. `QUALIFIED`: Complete hardware qualification suite passes: 24-hour soak test, thermal endurance (>60°C), watchdog hardware reset, and A/B dual-slot recovery.
5. `RELEASE_READY`: Signed production release with 100% release gates satisfied.

---

## Release Gating Checklist

| Release Gate | Verification Method | Required Status for `RELEASE_READY` |
|---|---|---|
| **Public API Isolation** | `test_public_headers` compilation | ✅ MUST PASS |
| **External Consumer** | `sdk-consumer-test` without source trees | ✅ MUST PASS |
| **CTest Suite** | 100% CTest pass rate (all unit tests) | ✅ MUST PASS |
| **Security Audit** | Zero hardcoded keys, TLS 8883, SHA-256 FOTA | ✅ MUST PASS |
| **Physical GPIO Matrix** | Pinout validation per `gpio-pin-validation.md`| ✅ MUST PASS ON TARGET |
| **Hardware Watchdog** | Physical starvation test forces SoC reset | ✅ MUST PASS ON TARGET |
| **Real Accelerator** | RKNN NPU / TensorRT GPU sustained inference | ✅ MUST PASS ON TARGET |
| **24-Hour Soak** | Zero memory growth & zero thread leaks | ✅ MUST PASS ON TARGET |

---

## Packaging & Build Command

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DSENZR_PLATFORM=auto
cmake --build . -j$(nproc)
ctest --output-on-failure
cpack -G TGZ
```
