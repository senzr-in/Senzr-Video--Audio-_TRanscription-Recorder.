#!/bin/bash
# Edge Gateway Framework - Shutdown Script
# Safely stops nginx, backend, dnsmasq, and hostapd.

set -e

echo "1/4 Stopping nginx..."
systemctl stop nginx || true

echo "2/4 Stopping edge-gateway (FastAPI backend)..."
systemctl stop edge-gateway || true

echo "3/4 Stopping dnsmasq (DHCP/DNS)..."
systemctl stop dnsmasq || true

echo "4/4 Stopping hostapd (WiFi AP)..."
systemctl stop hostapd || true

echo
echo "Stack is DOWN."
