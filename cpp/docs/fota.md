# FOTA (Firmware Over-The-Air) Subsystem

## Architectural Overview

`senzr::fota::Client` provides industrial A/B slot dual-partition firmware updates utilizing RAUC and direct bundle managers.

## Cryptographic Security Gates

Every firmware update strictly verifies:
1. HTTPS Transport with TLS certificate validation.
2. Mandatory SHA-256 bundle checksum validation before feeding payload to RAUC/flashing engine.
3. Post-install boot slot status check before issuing reboot commands.

## Code Example

```cpp
#include <senzr/fota.hpp>

senzr::fota::Client fota;
fota.on_progress([](int pct, const std::string& msg) {
    std::cout << "[" << pct << "%] " << msg << "\n";
});

auto res = fota.start_update({
    .manifest_url = "https://updates.senzr.io/fw/v1.2.0.raucb",
    .target_version = "1.2.0",
    .sha256_checksum = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    .auto_reboot = true
});
```
