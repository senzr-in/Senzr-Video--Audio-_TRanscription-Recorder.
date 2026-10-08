# Senzr SDK API Design & Architecture Standard

## 1. Public API Boundary Policy
- Official Public API: All headers located under `include/senzr/`.
- Internal Implementations: `src/**`, `include/app/**`, `session_pipeline/**`.
- Consumers linking against `Senzr::SDK` only have access to `include/senzr/` and do NOT require internal third-party headers (SQLite, Mosquitto, RKNN, OpenCV).

## 2. Universal Error Model
All operations returning fallible results use `senzr::Result<T>`:

```cpp
senzr::Result<senzr::SystemMetrics> metrics = dev.get_system_metrics();
if (metrics) {
    std::cout << "CPU Temp: " << metrics.value().cpu_temp_c << "\n";
} else {
    std::cerr << "Error: " << metrics.error().message() 
              << " (Code: " << static_cast<int>(metrics.error().code()) << ")\n";
}
```

## 3. Thread Safety Contract
| Subsystem | Thread-Safety Classification | Guarantees |
|---|---|---|
| `senzr::Device` | **Thread Safe** | Cached identity protected by `std::mutex`; telemetry methods are reentrant. |
| `senzr::gpio::Manager` | **Thread Safe** | Pin states, aliases, and pulse threads are synchronized internally. |
| `senzr::Storage` | **Thread Safe** | SQLite WAL mode permits concurrent reads; write mutex serializes mutations. |
| `senzr::fota::Client` | **Thread Safe** | Atomic test-and-set and file locks prevent duplicate update tasks. |
| `senzr::mqtt::Client` | **Thread Safe** | Background network thread with mutex-guarded publish/subscribe queues. |
| `senzr::log` | **Thread Safe** | Global log sink protected by atomic log levels and mutex-guarded custom sinks. |

## 4. Lifecycle & Ownership
- Resources follow **RAII** (Resource Acquisition Is Initialization).
- Public classes utilize the **Pimpl** idiom to maintain binary compatibility (ABI stability) and hide internal subsystem handles.
