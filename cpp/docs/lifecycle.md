# SDK Lifecycle & State Management

## Standard Module Lifecycle

All Senzr SDK subsystems adhere to explicit lifecycle stages:

```
[ Uninitialized ] ---> ( Construction )
         |
         v
[ Configured ] -------> ( initialize() / start() )
         |
         v
[ Running / Active ] -> ( stop() / shutdown() )
         |
         v
[ Stopped / Idle ] ---> ( Destruction )
```

### Subsystem Lifecycle Specifics

| Subsystem | Initialization Method | Shutdown Method | Background Thread Policy |
|---|---|---|---|
| `senzr::Device` | `get_instance()` (Lazy singleton) | Process exit | No background threads. |
| `senzr::gpio::Manager` | `init()` | Process exit | Pulse/safety monitors spawn explicit managed `std::jthread`. |
| `senzr::Storage` | `open(path)` | Destructor / RAII | Synchronous DB queries, thread-safe WAL connection. |
| `senzr::mqtt::Client` | `connect()` | `disconnect()` | Managed event loop with auto-reconnect backoff. |
| `senzr::fota::Client` | `start_update(...)` | `cancel()` | Managed async worker thread during download/verification. |
| `senzr::media::Recorder` | `start_recording(fn)` | `stop_recording()` | Async frame encoding worker thread. |
