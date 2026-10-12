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

echo "[start] bluetooth & BLE advertising"
rfkill unblock bluetooth || true
systemctl start bluetooth || true
hciconfig hci0 up || true

# Extended Advertising for Orange Pi 5 Max (AP6275P BT 5.3 / RK3588)
# Handle 0x01, properties 0x0013 (ADV_IND legacy connectable), Name "SENZR"
hcitool -i hci0 cmd 0x08 0x0039 00 00 >/dev/null 2>&1 || true
hcitool -i hci0 cmd 0x08 0x0035 01 F2 F5 ED 35 F5 C0 >/dev/null 2>&1 || true
hcitool -i hci0 cmd 0x08 0x0036 01 13 00 A0 00 00 00 01 00 07 01 00 00 00 00 00 00 00 00 7F 01 00 01 01 00 >/dev/null 2>&1 || true
# 4. Set Adv Data (Flags 3B + Name 7B + 128-bit Service UUID 18B = 28B <= 31B limit)
hcitool -i hci0 cmd 0x08 0x0037 01 03 01 1C 02 01 06 06 09 53 45 4E 5A 52 11 07 D4 C3 B2 A1 92 50 21 89 12 4C 34 7B 01 A0 82 9F >/dev/null 2>&1 || true
hcitool -i hci0 cmd 0x08 0x0039 01 01 01 00 00 00 >/dev/null 2>&1 || true

echo "[start] edge-gateway"
systemctl start edge-gateway

echo "[start] nginx"
systemctl start nginx

echo "[start] stack is up"

