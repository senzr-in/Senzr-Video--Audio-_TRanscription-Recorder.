#!/usr/bin/env bash
set -euo pipefail

echo "[start] assigning wlan0"
ip addr flush dev wlan0 || true
ip addr add 192.168.4.1/24 dev wlan0
ip link set wlan0 up

echo "[start] hostapd"
systemctl start hostapd

echo "[start] dnsmasq"
systemctl start dnsmasq

echo "[start] edge-gateway"
systemctl start edge-gateway

echo "[start] nginx"
systemctl start nginx

echo "[start] stack is up"
