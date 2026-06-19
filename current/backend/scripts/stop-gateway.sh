#!/bin/bash
# Edge Gateway - Stop Script
# Stops: edge-gateway-session (if running) + nginx + edge-gateway + dnsmasq + hostapd

echo "[1/5] Stopping edge-gateway-session (if active)..."
systemctl stop edge-gateway-session 2>/dev/null && echo "      edge-gateway-session stopped" || echo "      edge-gateway-session was not running"

echo "[2/5] Stopping nginx..."
systemctl stop nginx
echo "      nginx stopped"

echo "[3/5] Stopping edge-gateway (FastAPI backend)..."
systemctl stop edge-gateway
echo "      edge-gateway stopped"

echo "[4/5] Stopping dnsmasq..."
systemctl stop dnsmasq
echo "      dnsmasq stopped"

echo "[5/5] Stopping hostapd..."
systemctl stop hostapd
echo "      hostapd stopped"

echo ""
echo "✓ Gateway stack is DOWN."
