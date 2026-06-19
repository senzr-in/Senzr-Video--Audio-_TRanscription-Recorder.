0~#!/bin/bash

echo "[1/5] Stopping nginx..."
systemctl stop nginx

echo "[2/5] Stopping edge-gateway-session..."
systemctl stop edge-gateway-session

echo "[3/5] Stopping edge-gateway..."
systemctl stop edge-gateway

echo "[4/5] Stopping dnsmasq..."
systemctl stop dnsmasq

echo "[5/5] Stopping hostapd..."
systemctl stop hostapd

echo ""
echo "Stack is DOWN."

