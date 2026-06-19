#!/bin/bash

# Edge Gateway Framework - Startup Script
# Run this to bring up the full stack safely.

set -e  # stop on first error

echo "[1/6] Assigning static IP to wlan0..."
ip addr flush dev wlan0
ip addr add 192.168.4.1/24 dev wlan0
ip link set wlan0 up
echo "      wlan0 -> 192.168.4.1/24 [OK]"

echo "[2/6] Starting hostapd (Wi-Fi AP)..."
systemctl start hostapd
sleep 1
echo "      hostapd [OK]"

echo "[3/6] Starting dnsmasq (DHCP/DNS)..."
systemctl start dnsmasq
echo "      dnsmasq [OK]"

echo "[4/6] Starting edge-gateway (FastAPI backend)..."
systemctl start edge-gateway
echo "      edge-gateway [OK]"

echo "[5/6] Starting edge-gateway-session (recording pipeline)..."
systemctl start edge-gateway-session
echo "      edge-gateway-session [OK]"

echo "[6/6] Starting nginx (Web Server)..."
systemctl start nginx
echo "      nginx [OK]"

echo ""
echo "Stack is UP. Connect to 'EdgeGateway' and open:  http://192.168.4.1"
