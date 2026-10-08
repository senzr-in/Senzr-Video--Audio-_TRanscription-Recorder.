# Senzr SDK Thread Safety Guarantees

Every public class in `libsenzr` explicitly defines its concurrency contract:

| Subsystem / Class | Concurrency Model | Guarantee Details |
|---|---|---|
| `senzr::Device` | **Internally Synchronized** | Thread-safe singleton. Hardware read caches (MAC, Serial, Board) protected via `std::mutex`. |
| `senzr::log` | **Thread-Safe** | Global log sinks protected with internal atomic levels and output mutexes. |
| `senzr::gpio::Manager` | **Internally Synchronized** | Safe to pulse pins and poll states from multiple concurrent application threads. |
| `senzr::Storage` | **Serialized Writes / Concurrent Reads** | SQLite connection configured with WAL (Write-Ahead-Logging) and internal mutex serialization. |
| `senzr::mqtt::Client` | **Internally Synchronized** | Thread-safe `publish()`. Callbacks invoked from internal worker thread. |
| `senzr::fota::Client` | **Internally Synchronized** | Progress callbacks dispatched safely without blocking caller threads. |
| `senzr::ai::AnprEngine` | **Re-entrant / Thread-Safe** | Stateless plate formatting and normalization. Safe for multi-core concurrent execution. |
| `senzr::media::Recorder` | **External Synchronization** | Calling `start_recording` / `stop_recording` concurrently from multiple threads requires caller synchronization. |
