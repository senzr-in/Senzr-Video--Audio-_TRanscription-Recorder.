# Senzr Edge Gateway & SenzrOS — Complete Linux Setup & Guide Steps

A comprehensive, step-by-step production setup and operations guide for running **SenzrOS** and the **Senzr Edge Gateway** backend on Linux systems (Orange Pi 5 Max / Rockchip RK3588, ARM64, and generic Linux platforms).

---

## 📑 Table of Contents

1. [Hardware & System Requirements](#1-hardware--system-requirements)
2. [Step 1: Base Linux OS Installation & Bring-Up](#step-1-base-linux-os-installation--bring-up)
3. [Step 2: Post-Boot Linux System Tuning & Permissions](#step-2-post-boot-linux-system-tuning--permissions)
4. [Step 3: Installing Core Build Tools & Dependencies](#step-3-installing-core-build-tools--dependencies)
5. [Step 4: Cloning & Repository Setup](#step-4-cloning--repository-setup)
6. [Step 5: Compiling the C++ Core Engine & Utilities](#step-5-compiling-the-c-core-engine--utilities)
7. [Step 6: Hardware Peripherals Configuration (Camera, GPIO, NPU)](#step-6-hardware-peripherals-configuration-camera-gpio-npu)
8. [Step 7: Network Provisioning (Wi-Fi Hotspot, Captive Portal & Nginx)](#step-7-network-provisioning-wi-fi-hotspot-captive-portal--nginx)
9. [Step 8: System Configuration & Secrets Setup](#step-8-system-configuration--secrets-setup)
10. [Step 9: Production Systemd Daemons & Auto-Boot Setup](#step-9-production-systemd-daemons--auto-boot-setup)
11. [Step 10: Verification, Web Dashboard & Onboard CLI Operations](#step-10-verification-web-dashboard--onboard-cli-operations)
12. [Step 11: FOTA Firmware Packaging & Image Building](#step-11-fota-firmware-packaging--image-building)
13. [Troubleshooting & Diagnostic FAQ](#troubleshooting--diagnostic-faq)

---

## 1. Hardware & System Requirements

| Component | Minimum Specification | Recommended Specification |
|---|---|---|
| **SoC / CPU** | Quad-Core ARM64 or x86_64 (2.0 GHz) | Rockchip RK3588 / RK3588S (8-Core: 4x A76 + 4x A55) |
| **NPU** | Optional (fallback to CPU inference) | 6.0 TOPS Tri-Core RK3588 NPU |
| **RAM** | 2 GB LPDDR4/4x | 4 GB to 16 GB LPDDR5 |
| **Storage** | 16 GB MicroSD Card (Class 10 / U3) | 64 GB NVMe SSD (M.2 2280) or eMMC 5.1 |
| **Camera** | USB Video Class (UVC) Camera (`/dev/video0`) | 1080p@30fps USB UVC or MIPI-CSI Camera |
| **Relay / GPIO** | 3.3V / 5V Optocoupled Relay Module | 4-channel industrial optocoupled relay board |
| **Target OS** | Ubuntu 22.04 LTS (Jammy Jellyfish) ARM64 / Debian 11/12 | Ubuntu 22.04 LTS Rockchip Kernel 5.10 / 6.1 |

---

## Step 1: Base Linux OS Installation & Bring-Up

### 1.1 Download the Recommended Base Image
For the **Orange Pi 5 Max** (RK3588), download the pre-built Ubuntu 22.04 LTS Server image:
- **Image:** [Ubuntu 22.04 Server for Orange Pi 5 Max](https://github.com/Joshua-Riek/ubuntu-rockchip/releases)
- Alternative: Official Orange Pi OS (Ubuntu 22.04 / Debian Bullseye).

### 1.2 Flash the OS Image
Using **BalenaEtcher**, **Raspberry Pi Imager**, or Linux `dd`:

```bash
# Decompress and flash using dd on Linux (replace /dev/sdX with your target card/NVMe drive)
xz -d ubuntu-22.04-preinstalled-server-arm64-orangepi-5-max.img.xz
sudo dd if=ubuntu-22.04-preinstalled-server-arm64-orangepi-5-max.img of=/dev/sdX bs=4M status=progress conv=fsync
```

### 1.3 Boot and Initial SSH Access
1. Insert the microSD/eMMC or install the NVMe drive and power the board via a certified **5V/4A or 5V/5A USB-C** power supply.
2. Connect an Ethernet cable to your local router or switch.
3. Identify the board's IP address (from your router DHCP client list or via `nmap -sP 192.168.1.0/24`).
4. Connect via SSH:
   ```bash
   ssh orangepi@<BOARD_IP_ADDRESS>
   # Default password is usually 'orangepi'
   ```

---

## Step 2: Post-Boot Linux System Tuning & Permissions

Once logged into the board via SSH, run the following essential system adjustments:

### 2.1 Configure Passwordless Sudo (Required for Headless Daemons)
To allow the edge gateway services and automated update scripts to control systemd without interactive password prompts:

```bash
sudo bash -c 'cat << "EOF" > /etc/sudoers.d/010_orangepi-nopasswd
orangepi ALL=(ALL) NOPASSWD: ALL
EOF'
sudo chmod 0440 /etc/sudoers.d/010_orangepi-nopasswd
```

### 2.2 Configure Polkit Rule for Systemd Units
Allows non-root service management:
```bash
sudo mkdir -p /etc/polkit-1/rules.d
sudo bash -c 'cat << "EOF" > /etc/polkit-1/rules.d/50-senzr-systemd.rules
polkit.addRule(function(action, subject) {
    if (action.id.indexOf("org.freedesktop.systemd1.manage-units") == 0) {
        return polkit.Result.YES;
    }
});
EOF'
sudo chmod 0644 /etc/polkit-1/rules.d/50-senzr-systemd.rules
```

### 2.3 Set Timezone, NTP & Performance Governor
```bash
# Set Timezone (e.g., Asia/Kolkata or UTC)
sudo timedatectl set-timezone Asia/Kolkata
sudo timedatectl set-ntp true

# Set CPU Performance Governor for maximum inference throughput
echo "performance" | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null || true

# Add a 2GB Swapfile (avoids OOM during multi-core C++ compilation)
if [ ! -f /swapfile ]; then
    sudo fallocate -l 2G /swapfile
    sudo chmod 600 /swapfile
    sudo mkswap /swapfile
    sudo swapon /swapfile
    echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
fi
```

---

## Step 3: Installing Core Build Tools & Dependencies

Update apt repositories and install all essential C++ compilers, libraries, and networking packages:

```bash
sudo apt-get update && sudo apt-get upgrade -y
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git \
    curl \
    wget \
    zip \
    unzip \
    tar \
    v4l-utils \
    alsa-utils \
    gpiod \
    libgpiod-dev \
    libopencv-dev \
    libsqlite3-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    nlohmann-json3-dev \
    libmosquitto-dev \
    nginx \
    hostapd \
    dnsmasq
```

### Check Installed Dependency Versions:
```bash
cmake --version          # Recommended: >= 3.16
gcc --version            # Recommended: >= 11.2 (C++17 support)
pkg-config --modversion opencv4
sqlite3 --version
```

---

## Step 4: Cloning & Repository Setup

### 4.1 Clone or Sync the Project to the Board
Place the project in the standard directory `/home/orangepi/edge-gateway` (or `/opt/edge-gateway`):

```bash
# Option A: Clone directly via Git
git clone https://github.com/daltonium/edge-gateway-framework.git /home/orangepi/edge-gateway

# Option B: Or copy from your PC via rsync/scp:
# scp -r F:\PI5MAX\* orangepi@<BOARD_IP>:/home/orangepi/edge-gateway/
```

### 4.2 Establish Directory Structure
```bash
cd /home/orangepi/edge-gateway
mkdir -p build downloads releases current/database current/models
```

---

## Step 5: Compiling the C++ Core Engine & Utilities

The Senzr Edge Gateway features an optimized C++17 core engine built with CMake.

### 5.1 Navigate to the C++ Source Tree
```bash
cd /home/orangepi/edge-gateway/cpp
```

### 5.2 Configure CMake in Release Mode
```bash
# Clean previous build cache if switching hardware
rm -rf build
mkdir -p build && cd build

# Configure CMake
cmake -DCMAKE_BUILD_TYPE=Release ..
```

### 5.3 Compile Using All CPU Cores
```bash
make -j$(nproc)
```

Upon completion, the following core executables are built inside `build/`:
- **`edge-gateway-backend`**: Core edge daemon hosting the HTTP REST API (port 8000), real-time camera processing, ANPR, S3 upload worker, and relay barrier control.
- **`edge-gateway-updater`**: FOTA background daemon with rollback protection and S3/MQTT pollers.
- **`senzr-ctl`**: Standalone administrative command-line utility for device health, GPIO pulses, and telemetry.

---

## Step 6: Hardware Peripherals Configuration (Camera, GPIO, NPU)

### 6.1 Camera (UVC / USB) Setup
1. Connect your USB camera to a **USB 3.0** port (blue port) on the board.
2. Verify video device nodes:
   ```bash
   v4l2-ctl --list-devices
   ls -l /dev/video*
   ```
3. Grant camera permissions to the `orangepi` user:
   ```bash
   sudo usermod -aG video orangepi
   ```
4. Verify supported video formats and resolutions:
   ```bash
   v4l2-ctl -d /dev/video0 --list-formats-ext
   ```

### 6.2 Industrial GPIO Configuration
The gateway uses `libgpiod` character device drivers for deterministic, sub-millisecond physical access control.

1. Inspect available GPIO chips:
   ```bash
   gpiodetect
   # Typical RK3588 outputs: gpiochip0 to gpiochip4
   ```
2. Grant GPIO group permissions:
   ```bash
   sudo usermod -aG dialout,gpio orangepi 2>/dev/null || true
   ```
3. Default Pin Mappings (Configurable in `/etc/senzr/senzr.conf`):
   - **Door Relay:** Pin `44`
   - **Buzzer:** Pin `45`
   - **Status LED:** Pin `46`
   - **Door / Safety Sensor:** Pin `47`
   - **Wiegand D0 / D1:** Pins `48` & `49`

### 6.3 NPU Acceleration (RK3588 NPU Driver Verification)
Check that the Rockchip NPU kernel module is loaded:
```bash
dmesg | grep -i rknpu
ls -l /dev/rknpu*
# Should show /dev/rknpu (crw-rw-rw- root root)
```

---

## Step 7: Network Provisioning (Wi-Fi Hotspot, Captive Portal & Nginx)

The gateway supports **Zero-Internet Provisioning** via an onboard Wi-Fi Access Point and captive portal.

### 7.1 Wi-Fi Access Point (`hostapd`)
Copy and enable the hotspot configuration:

```bash
sudo cp /home/orangepi/edge-gateway/deploy/network/hostapd.conf /etc/hostapd/hostapd.conf

# Unmask and configure hostapd
sudo rfkill unblock wlan || true
sudo systemctl unmask hostapd
sudo systemctl enable hostapd
```

### 7.2 DHCP & Captive DNS Server (`dnsmasq`)
Copy the local DHCP & captive portal configuration:

```bash
sudo cp /home/orangepi/edge-gateway/deploy/network/dnsmasq.conf /etc/dnsmasq.d/edge-gateway.conf
sudo systemctl restart dnsmasq
sudo systemctl enable dnsmasq
```
*All DNS requests through the Wi-Fi AP resolve directly to `192.168.4.1`.*

### 7.3 Web Dashboard & Reverse Proxy (`nginx`)
Configure Nginx to serve the static frontend on port 80, redirect OS captive portal probes (`generate_204`, `hotspot-detect.html`, `ncsi.txt`), and reverse-proxy `/api/` calls to the C++ backend on port 8000:

```bash
sudo cp /home/orangepi/edge-gateway/deploy/network/nginx-edge-gateway.conf /etc/nginx/sites-available/default
sudo nginx -t
sudo systemctl restart nginx
sudo systemctl enable nginx
```

---

## Step 8: System Configuration & Secrets Setup

### 8.1 Global Platform Configuration (`/etc/senzr/senzr.conf`)
Create the system configuration directory and copy the template:

```bash
sudo mkdir -p /etc/senzr /etc/senzr/certs
sudo cp /home/orangepi/edge-gateway/deploy/etc/senzr/senzr.conf.example /etc/senzr/senzr.conf
```

Inspect and adjust `/etc/senzr/senzr.conf`:
```ini
[device]
primary_interface = eth0

[fota]
s3_bucket = senzr-fota-storage
s3_region = us-east-1
s3_prefix = app-releases
reboot_after_update = false
check_interval_sec = 60
app_service = senzr-edge.service
health_check_url = http://127.0.0.1:8000/api/status

[mqtt]
broker = mqtt.fieldseasy.com
port = 1883
user = iot-device
password = Senzr123
broadcast_topic = edge-gateway/update

[gpio]
door_relay_pin = 44
buzzer_pin = 45
status_led_pin = 46
door_sensor_pin = 47
```

### 8.2 Production Deployment Directory Structure (`/opt/edge-gateway`)
Prepare the production runtime directory:

```bash
sudo mkdir -p /opt/edge-gateway/bin \
            /opt/edge-gateway/database \
            /opt/edge-gateway/models \
            /opt/edge-gateway/logs \
            /opt/edge-gateway/downloads \
            /opt/edge-gateway/backup \
            /opt/edge-gateway/frontend

# Copy compiled binaries
sudo cp /home/orangepi/edge-gateway/cpp/build/edge-gateway-backend /opt/edge-gateway/bin/
sudo cp /home/orangepi/edge-gateway/cpp/build/edge-gateway-updater /opt/edge-gateway/bin/
sudo cp /home/orangepi/edge-gateway/cpp/build/senzr-ctl /opt/edge-gateway/bin/
sudo chmod +x /opt/edge-gateway/bin/*

# Create global symlinks
sudo ln -sf /opt/edge-gateway/bin/senzr-ctl /usr/local/bin/senzr-ctl
sudo ln -sf /opt/edge-gateway/bin/edge-gateway-updater /usr/local/bin/edge-gateway-updater

# Copy frontend assets for Nginx
sudo cp -r /home/orangepi/edge-gateway/current/frontend/* /opt/edge-gateway/frontend/ 2>/dev/null || \
sudo cp -r /home/orangepi/edge-gateway/cpp/frontend/* /opt/edge-gateway/frontend/ 2>/dev/null || true
```

---

## Step 9: Production Systemd Daemons & Auto-Boot Setup

SenzrOS provides automated systemd service units for automatic recovery, watchdog supervision, and FOTA rollbacks.

### 9.1 Install Systemd Service Units
```bash
# 1. Main Core Engine Service
sudo cp /home/orangepi/edge-gateway/deploy/systemd/edge-gateway.service /etc/systemd/system/senzr-edge.service

# 2. FOTA Updater Service
sudo cp /home/orangepi/edge-gateway/deploy/systemd/edge-gateway-updater.service /etc/systemd/system/senzr-updater.service

# 3. Rollback Failure Handler Service
sudo cp /home/orangepi/edge-gateway/deploy/systemd/edge-gateway-rollback.service /etc/systemd/system/senzr-rollback.service

# Reload systemd
sudo systemctl daemon-reload
```

### 9.2 Enable and Start Services
```bash
# Enable services on boot
sudo systemctl enable senzr-edge.service
sudo systemctl enable senzr-updater.service

# Start the core engine
sudo systemctl start senzr-edge.service
```

### 9.3 Verify Service Health & View Live Logs
```bash
# Check status
sudo systemctl status senzr-edge.service

# Stream live real-time logs
sudo journalctl -u senzr-edge.service -f
```

---

## Step 10: Verification, Web Dashboard & Onboard CLI Operations

### 10.1 Access the Web Dashboard
From a PC, tablet, or smartphone connected to the same network (or the `EdgeGateway` Wi-Fi hotspot):

Open your web browser and navigate to:
```
http://<BOARD_IP>:8000     (Direct Backend Stream & API)
http://<BOARD_IP>/          (Through Nginx Reverse Proxy)
http://192.168.4.1/         (Via Wi-Fi Hotspot)
```

**Features Available:**
- **Live Optical Stream**: Low-latency video feed with neon HUD bounding boxes.
- **Vision Modes**: Switch between `All Combined`, `Vehicles Only`, `Number Plates (ANPR)`, and `Persons Only`.
- **Media Gallery**: Review captured snapshot clips and event recordings.
- **Hardware Telemetry**: Real-time CPU temperature, RAM usage, storage load, and network connection status.

### 10.2 Command-Line Interface (`senzr-ctl`)
Run on the board terminal to diagnose and control hardware:

```bash
# 1. Display Hardware Specs, MAC, IP, Temperature, and Telemetry:
senzr-ctl device info

# 2. Check FOTA and Firmware Version Status:
senzr-ctl fota status

# 3. Test GPIO Door Relay (Pulse relay for 3 seconds / 3000ms):
senzr-ctl gpio pulse DOOR_RELAY 3000

# 4. List all mapped GPIO pins and states:
senzr-ctl gpio list

# 5. Inspect Ethernet and Wi-Fi Network Status:
senzr-ctl net status

# 6. Reboot the system safely after a grace period:
senzr-ctl device reboot 3
```

---

## Step 11: FOTA Firmware Packaging & Image Building

### 11.1 Build a FOTA Release Package (.zip + SHA-256 Manifest)
To generate an automated, signed release package for OTA deployment:

```bash
cd /home/orangepi/edge-gateway/cpp
bash build.sh 1.0.7
```
This automatically compiles the project, stages binaries and frontend assets, generates SHA-256 checksums, and creates:
- `releases/edge-gateway-v1.0.7.zip`
- `releases/latest.json`

### 11.2 Build a Flashable MicroSD/eMMC Image
To generate a complete, flashable image (.img) containing the kernel, OS, and pre-installed Senzr Edge Gateway:

```bash
cd /home/orangepi/edge-gateway/deploy
sudo bash build_image.sh
```
The output file will be written to `deploy/output/edge-gateway-orangepi5max.img`.

---

## Troubleshooting & Diagnostic FAQ

### Q1: `v4l2-ctl` shows no camera or video device `/dev/video0` is missing
- Check that the camera is plugged into a functional USB 3.0 port: `lsusb`.
- Verify kernel dmesg: `dmesg | grep -i uvcvideo`.
- If permission denied: Ensure user is in the `video` group (`sudo usermod -aG video $USER`) and re-login.

### Q2: Port 8000 or Port 80 is already in use
- Identify the process holding the port:
  ```bash
  sudo lsof -i :8000
  sudo lsof -i :80
  ```
- Terminate competing processes or restart the service:
  ```bash
  sudo systemctl restart senzr-edge.service
  ```

### Q3: Wi-Fi Hotspot does not broadcast SSID
- Check if Wi-Fi interface is RF-killed (hardware/software block):
  ```bash
  sudo rfkill list
  sudo rfkill unblock wlan
  ```
- Restart hostapd and check its logs:
  ```bash
  sudo systemctl restart hostapd
  sudo journalctl -u hostapd -n 50 --no-pager
  ```

### Q4: SQLite database locked error
- SenzrOS uses **Write-Ahead Logging (WAL)** for concurrent read/write transactions. Ensure the database directory is writable:
  ```bash
  sudo chown -R orangepi:orangepi /opt/edge-gateway/database
  sudo chmod 755 /opt/edge-gateway/database
  ```

### Q5: System temperature too high under heavy AI workload
- Check current temperature:
  ```bash
  cat /sys/class/thermal/thermal_zone*/temp
  # Divide by 1000 for degrees Celsius
  ```
- Ensure heatsink and active cooling fan are securely seated on the RK3588 SoC.

---

## 📌 Summary Checklist for New Deployments

- [ ] Flash base Ubuntu 22.04 LTS arm64 image.
- [ ] Connect via SSH and set passwordless sudo + polkit rules.
- [ ] Install build dependencies (`apt-get install -y build-essential cmake ...`).
- [ ] Compile C++ backend in Release mode (`cmake -DCMAKE_BUILD_TYPE=Release .. && make -j8`).
- [ ] Copy binaries to `/opt/edge-gateway/bin/` and configure `/etc/senzr/senzr.conf`.
- [ ] Enable and start `senzr-edge.service` and `senzr-updater.service`.
- [ ] Test camera feed on `http://<IP>:8000` or `http://<IP>/`.
- [ ] Verify hardware pulse with `senzr-ctl gpio pulse DOOR_RELAY 3000`.
