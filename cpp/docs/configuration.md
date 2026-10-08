# Structured Configuration Standard

All SDK configurations use explicit strongly typed structures located in their respective module headers.

## Configuration Types

- `senzr::mqtt::Config`:
  - `broker_host` (default: `"api.accesteasy.io"`)
  - `broker_port` (default: `8883` MQTTS)
  - `uuid` (hardware derived)
  - `token` (auth token)
  - `use_tls` (default: `true`)
  - `ca_cert_path` (default: `"/etc/ssl/certs/ca-certificates.crt"`)

- `senzr::fota::Config`:
  - `manifest_url`
  - `target_version`
  - `sha256_checksum` (Mandatory cryptographic signature verification)
  - `auto_reboot` (default: `false`)

- `senzr::ai::Config`:
  - `model_path`
  - `npu_core_mask` (Core 0, 1, 2)
  - `confidence_threshold`
