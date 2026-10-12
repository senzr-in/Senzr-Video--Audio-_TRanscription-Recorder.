#!/usr/bin/env bash
# ==============================================================================
# SENZR Edge Gateway - Automated Zero-Touch Bootstrap & Dependency Installer
# Installs all required OS packages, libraries, Bluetooth/BLE tools, builds C++,
# and configures services automatically without manual intervention.
# ==============================================================================
set -euo pipefail

echo "================================================================="
echo "   🚀 SENZR EDGE GATEWAY - AUTOMATED ZERO-TOUCH BOOTSTRAP       "
echo "================================================================="

# 1. Check Root Privileges
if [ "$EUID" -ne 0 ]; then
  SUDO="sudo"
else
  SUDO=""
fi

# 2. Automatically install required system packages if missing
echo "[1/4] Checking and installing system dependencies..."
REQUIRED_PKGS=(
  build-essential
  cmake
  pkg-config
  libopencv-dev
  libsqlite3-dev
  libcurl4-openssl-dev
  libssl-dev
  nlohmann-json3-dev
  libmosquitto-dev
  libgpiod-dev
  bluez
  bluetooth
  rfkill
  python3-dbus
  python3-gi
)

MISSING_PKGS=()
for pkg in "${REQUIRED_PKGS[@]}"; do
  if ! dpkg -s "$pkg" >/dev/null 2>&1; then
    MISSING_PKGS+=("$pkg")
  fi
done

if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
  echo "      Installing missing packages: ${MISSING_PKGS[*]}"
  $SUDO apt-get update -qq
  $SUDO apt-get install -y --no-install-recommends "${MISSING_PKGS[@]}"
else
  echo "      All required packages already installed!"
fi

# 3. Configure Bluetooth & BLE Controller
echo "[2/4] Initializing Bluetooth hardware & BLE stack..."
$SUDO systemctl enable --now bluetooth || true
$SUDO rfkill unblock bluetooth 2>/dev/null || true
$SUDO hciconfig hci0 up 2>/dev/null || true
# Extended Advertising for Orange Pi 5 Max (AP6275P BT 5.3)
$SUDO hcitool -i hci0 cmd 0x08 0x0039 00 00 >/dev/null 2>&1 || true
$SUDO hcitool -i hci0 cmd 0x08 0x0035 01 F2 F5 ED 35 F5 C0 >/dev/null 2>&1 || true
$SUDO hcitool -i hci0 cmd 0x08 0x0036 01 13 00 A0 00 00 00 01 00 07 01 00 00 00 00 00 00 00 00 7F 01 00 01 01 00 >/dev/null 2>&1 || true
$SUDO hcitool -i hci0 cmd 0x08 0x0037 01 03 01 0A 02 01 06 06 09 53 45 4E 5A 52 >/dev/null 2>&1 || true
$SUDO hcitool -i hci0 cmd 0x08 0x0039 01 01 01 00 00 00 >/dev/null 2>&1 || true
echo "      Bluetooth & BLE controller initialized!"

# 4. Compile the C++ Subsystem
echo "[3/4] Compiling Edge Gateway C++ Subsystem..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
CPP_DIR="$ROOT_DIR/cpp"

cd "$CPP_DIR"
mkdir -p build_linux && cd build_linux
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j"$(nproc)" edge-gateway-backend senzr-ctl test_ble

# 5. Run Verification Self-Test
echo "[4/4] Executing BLE Access Subsystem Self-Test..."
./test_ble

echo "================================================================="
echo "   ✅ SENZR EDGE GATEWAY BOOTSTRAP COMPLETE!"
echo "   Run backend now via: ./edge-gateway-backend"
echo "   Or check status via: ./senzr-ctl ble status"
echo "================================================================="
