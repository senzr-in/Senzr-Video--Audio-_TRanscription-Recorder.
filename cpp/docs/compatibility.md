# ABI & Platform Compatibility Matrix

## Supported Compiler & Toolchains

| OS / Distribution | Target Architecture | Compiler | C++ Standard | Supported |
|---|---|---|---|---|
| Ubuntu 22.04 LTS (Jammy) | aarch64 (ARM64) | GCC 11.4+ / Clang 14+ | C++20 | ✅ Certified |
| Debian 12 (Bookworm) | aarch64 (ARM64) | GCC 12.2+ | C++20 | ✅ Certified |
| Ubuntu 24.04 LTS (Noble) | aarch64 (ARM64) | GCC 13.2+ | C++20 | ✅ Certified |
| WSL2 (Ubuntu 22.04) | x86_64 / Host Sim | GCC 11.4+ | C++20 | ✅ Development |

## Hardware Platform Support

- Orange Pi 5 Max (Rockchip RK3588, 16GB/32GB)
- Rock 5B / Radxa 5 (Rockchip RK3588)
- FriendlyElec NanoPC-T6 (Rockchip RK3588)
- Senzr Edge Appliance Rev 1.0 / 2.0

## ABI Discipline Policy

- Public classes implement the Pimpl idiom to isolate STL containers and third-party struct layout from client compilation units.
- Deprecation cycle: APIs marked `[[deprecated("reason")]]` remain available for 1 minor version cycle before removal in major increments.
