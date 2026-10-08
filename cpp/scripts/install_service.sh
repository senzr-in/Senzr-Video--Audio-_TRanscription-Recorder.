#!/usr/bin/env bash
# ==============================================================================
# Senzr Edge Gateway - Standard Production Service Installer
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_SRC="${SCRIPT_DIR}/senzr-edge.service"
SERVICE_DEST="/etc/systemd/system/senzr-edge.service"

echo "======================================================"
echo "  🚀 INSTALLING SENZR EDGE GATEWAY PRODUCTION SERVICE"
echo "======================================================"

if [[ $EUID -ne 0 ]]; then
    echo "⚠️  This installer must be run with sudo/root privileges."
    echo "    Usage: sudo bash scripts/install_service.sh"
    exit 1
fi

# 1. Copy service unit to systemd directory
echo "[1/4] Copying service configuration to ${SERVICE_DEST}..."
cp -f "${SERVICE_SRC}" "${SERVICE_DEST}"
chmod 644 "${SERVICE_DEST}"

# 2. Reload systemd daemon
echo "[2/4] Reloading systemd daemon..."
systemctl daemon-reload

# 3. Enable service on startup / boot
echo "[3/4] Enabling senzr-edge.service for automatic power-on boot..."
systemctl enable senzr-edge.service

# 4. Start or restart service immediately
echo "[4/4] Starting senzr-edge.service..."
systemctl restart senzr-edge.service

echo -e "\n======================================================"
echo "  ✅ SENZR EDGE SERVICE INSTALLED & STARTED"
echo "------------------------------------------------------"
echo "  Status:"
systemctl status senzr-edge.service --no-pager -l
echo "======================================================"
