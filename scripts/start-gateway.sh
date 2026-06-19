#!/bin/bash

# Edge Gateway Framework - Startup Script

set -e

echo "[1/5] Assigning static IP to wlan0..."
ip addr flush dev wlan0
ip addr add 192.168.4.1/24 dev wlan0
ip link set wlan0 up
echo "      wlan0 -> 192.168.4.1/24 [OK]"

echo "[2/5] Starting hostapd (Wi-Fi AP)..."
systemctl start hostapd
sleep 1
echo "      hostapd [OK]"

echo "[3/5] Starting dnsmasq (DHCP/DNS)..."
systemctl start dnsmasq
echo "      dnsmasq [OK]"

echo "[4/5] Starting edge-gateway (FastAPI backend)..."
systemctl start edge-gateway
echo "      edge-gateway [OK]"

echo "[5/5] Starting nginx (Web Server)..."
systemctl start nginx
echo "      nginx [OK]"

echo ""
echo "Stack is UP. Connect to 'EdgeGateway' and open: http://192.168.4.1"
