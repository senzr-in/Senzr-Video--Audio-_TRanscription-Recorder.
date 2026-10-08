# Senzr Edge Gateway - Step-by-Step SSH Build & Deployment Guide

This guide walks you through connecting to your **Orange Pi 5 Max** via **SSH** and compiling, running, and managing the C++ backend directly on the board.

---

## 📋 System Details (Orange Pi 5 Max)
- **Default Username:** `orangepi`
- **Default Project Path:** `/home/orangepi/edge-gateway/cpp`
- **Service Name:** `senzr-edge.service`
- **Web Dashboard:** `http://<ORANGE_PI_IP>:8000`

---

## 🚀 Step 1: Connect to Orange Pi 5 Max via SSH

From PowerShell or any terminal on your PC:

```powershell
ssh orangepi@<ORANGE_PI_IP>
```
*(Replace `<ORANGE_PI_IP>` with your board's IP address, e.g., `192.168.1.150` or `192.168.4.1`)*

---

## 🛠️ Step 2: Install Dependencies on the Board (First Time Only)

Run once on the Orange Pi terminal:

```bash
sudo apt-get update && sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libopencv-dev \
    libsqlite3-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    nlohmann-json3-dev \
    libmosquitto-dev \
    libgpiod-dev \
    zip
```

---

## 🔨 Step 3: Step-by-Step Native Compilation via SSH

Once logged in via SSH:

### 1. Navigate to the C++ directory:
```bash
cd /home/orangepi/edge-gateway/cpp
```

### 2. Create and enter the build directory:
```bash
mkdir -p build
cd build
```

### 3. Configure CMake in Release mode:
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

### 4. Compile using all 8 CPU cores:
```bash
make -j$(nproc)
```

---

## 🏃 Step 4: Run & Test the Backend

### Option A: Run Interactively in Foreground (See Live Logs & Object/Plate Detection)
```bash
cd /home/orangepi/edge-gateway/cpp/build
./edge-gateway-backend
```

You will see the startup banner:
```
======================================================
  🚀 SENZR EDGE GATEWAY C++ CORE ENGINE
  📡 Listening on: 0.0.0.0:8000
  ⚡ Vision Mode: Object & Number Plate Recognition
======================================================
```

### Option B: Run as a Background System Service
```bash
# If not installed yet, install the service:
sudo bash /home/orangepi/edge-gateway/cpp/scripts/install_service.sh

# Or restart the existing service:
sudo systemctl restart senzr-edge.service

# Check service status and live logs:
sudo systemctl status senzr-edge.service
sudo journalctl -u senzr-edge.service -f
```

---

## 🌐 Step 5: Test from Your PC Browser

Open your web browser on your PC and visit:
```
http://<ORANGE_PI_IP>:8000
```

1. **Optical Stream:** Live RTSP/USB camera feed with neon HUD reticles.
2. **AI Detection Modes:** Switch between `All Combined`, `Vehicles Only`, `Number Plates`, and `Persons Only`.
3. **Media Gallery:** Check captured snapshots under the `Media Gallery -> 🚗 Plates / 👤 Persons` tab.

---

## 📦 Step 6: Quick 1-Liner Rebuild Command via SSH

Whenever you make code edits, you can rebuild and restart in one command from your PC terminal:

```powershell
ssh orangepi@<ORANGE_PI_IP> "cd /home/orangepi/edge-gateway/cpp/build && make -j\$(nproc) && sudo systemctl restart senzr-edge.service"
```
