# Edge Gateway Framework

An Edge AI gateway running on Orange Pi 5 Max (Debian).
Provides a local Wi-Fi hotspot with a captive portal for configuring
AI detection services (face, object, etc.) — all on-device, no cloud.

## Stack
- OS: Debian (Bookworm) on Orange Pi 5 Max
- Backend: FastAPI (Python)
- Frontend: HTML + CSS + JS (captive portal)
- Storage: SQLite + JSON
- Wi-Fi: hostapd + dnsmasq
- Service Control: systemd
