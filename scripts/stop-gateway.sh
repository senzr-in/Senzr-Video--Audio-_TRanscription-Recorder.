#!/bin/bash

# Edge Gateway Framework - Shutdown Script

echo "[1/4] Stopping nginx..."
systemctl stop nginx

echo "[2/4] Stopping edge-gateway (FastAPI)..."
systemctl stop edge-gateway

echo "[3/4] Stopping dnsmasq..."
systemctl stop dnsmasq

echo "[4/4] Stopping hostapd..."
systemctl stop hostapd

echo ""
echo "Stack is DOWN."
