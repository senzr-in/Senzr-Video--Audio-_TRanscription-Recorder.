#!/bin/bash
# Edge Gateway - Start Script
# Controls: Hotspot (wlan0) + edge-gateway (FastAPI) + nginx
# Does NOT start edge-gateway-session (manage separately)

set -e

echo "[1/6] Flushing and assigning static IP to wlan0..."
ip addr flush dev wlan0
ip addr add 192.168.4.1/24 dev wlan0
ip link set wlan0 up
echo "      wlan0 → 192.168.4.1 OK"

echo "[2/6] Starting hostapd (Wi-Fi AP)..."
systemctl start hostapd
sleep 1
echo "      hostapd OK"

echo "[3/6] Starting dnsmasq (DHCP/DNS)..."
systemctl start dnsmasq
echo "      dnsmasq OK"

echo "[4/6] Starting edge-gateway (FastAPI backend)..."
systemctl start edge-gateway
echo "      edge-gateway OK"

echo "[5/6] Starting nginx..."
systemctl start nginx
echo "      nginx OK"

echo "[6/6] Checking camera control via web UI..."
echo "      Camera ON/OFF is controlled via http://192.168.4.1"

echo ""
echo "✓ Gateway stack is UP."
echo "  Connect to 'EdgeGateway' WiFi and open http://192.168.4.1"
echo ""
echo "  To start session pipeline separately:"
echo "    sudo systemctl start edge-gateway-session"
