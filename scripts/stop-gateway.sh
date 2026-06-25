#!/usr/bin/env bash
set -euo pipefail

echo "[stop] nginx"
systemctl stop nginx || true

echo "[stop] edge-gateway"
systemctl stop edge-gateway || true

echo "[stop] dnsmasq"
systemctl stop dnsmasq || true

echo "[stop] hostapd"
systemctl stop hostapd || true

echo "[stop] stack is down"
