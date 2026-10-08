# Senzr SDK Security Architecture & Release Gates

## 1. Zero Plaintext Credentials
- Public configuration structures (`MqttConfig`, `FotaConfig`, `StorageConfig`) require explicit caller parameters or environment variables (`.env`).
- No default hardcoded passwords or broker credentials exist in public headers.

## 2. Mandatory Transport Encryption
- Default MQTT port is `8883` (MQTTS).
- TLS 1.3 encryption is active by default with CA certificate validation and Server Name Indication (SNI).

## 3. Cryptographic FOTA Package Verification
- All OTA/FOTA packages require pre-flight SHA-256 validation against signed metadata before extraction.
- Automatic rollback occurs if health-check endpoints fail upon post-update boot.

## 4. Input Sanitization & Path Traversal Prevention
- Direct `std::system()` command invocations are restricted; version strings and file paths are validated against whitelist expressions to prevent shell injection or zip-slip path traversal (`../`).
