# MQTT Telemetry & Remote Control

## Subsystem Overview

`senzr::mqtt::Client` provides cloud synchronization for AccessEasy platforms with automatic backoff reconnection, heartbeat telemetry, and remote action dispatch.

## Default TLS Hardening

- All connections default to MQTTS on Port `8883`.
- Plaintext defaults are disabled by default in production.
- TLS 1.3 encryption with system CA verification is enforced.

## Code Example

```cpp
#include <senzr/mqtt/client.hpp>

senzr::mqtt::Client client;
client.set_config({
    .broker_host = "mqtt.senzr.io",
    .broker_port = 8883,
    .use_tls = true
});

client.on_message([](const std::string& topic, const std::string& payload) {
    std::cout << "Received on " << topic << ": " << payload << "\n";
});

client.connect();
```
