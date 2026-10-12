# SenzrOS — Edge Operating System Specification

**Operating System:** SenzrOS (Senzr EdgeOS)  
**System Architecture:** ARM64 / aarch64  
**Primary SoC Platform:** Rockchip RK3588 / RK3588S  
**Target Reference Boards:** Orange Pi 5 Max, Radxa Rock 5B, FriendlyElec NanoPC-T6, Khadas Edge2  
**Target Applications:** Smart Access Control, AI Security Gateways, Industrial Edge Vision Appliances  

---

## 1. Operating System Overview

**SenzrOS** is a specialized, industrial-grade Linux edge operating system engineered for autonomous access control, real-time edge computer vision, and secure IoT telemetry. 

Unlike general-purpose Linux distributions, SenzrOS is purpose-built to operate at the network edge with full functional autonomy. It guarantees instant physical access verification, high-throughput neural network inference, and local credential matching—even during prolonged internet, local area network, or cloud server outages. When connectivity is restored, SenzrOS automatically synchronizes events, audit trails, and security logs with the cloud.

### Core Operating Principles
- **Deterministic Edge Execution:** Low-latency physical I/O reaction times for access barriers, electric strikes, and safety interlocks.
- **Hardware-Accelerated AI:** Deep neural network models run directly on the SoC's dedicated Neural Processing Unit (NPU), preserving CPU cores for communications and operational logic.
- **Offline Autonomy:** Complete credential databases, plate recognition rules, and session logs reside locally in encrypted, power-fail-safe storage.
- **Resilient Lifecycle Management:** Dual-slot A/B partition updates with automated rollback, combined with hardware and system-level watchdog monitoring to prevent bricking or downtime in unmanned installations.

---

## 2. System Architecture & Layers

SenzrOS is structured in modular operational layers to isolate hardware drivers from business logic and network communications:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        User & Management Layer                         │
│   • Local Web Management Console      • Zero-Config Captive Portal     │
│   • Onboard Command-Line Interface    • Secure REST Integration API    │
├────────────────────────────────────────────────────────────────────────┤
│                       Application & Services Layer                     │
│   • Access Control State Machine      • Event-Driven Recording Engine  │
│   • Offline Credential Verifier       • Cloud Synchronization Daemon   │
│   • BLE Commissioning Supervisor      • Firmware Update Manager (FOTA) │
├────────────────────────────────────────────────────────────────────────┤
│                         Edge Intelligence Layer                        │
│   • Multi-Core NPU Inference Pipeline • Real-time Person Detection     │
│   • MoRTH & Bharat (BH) ANPR Engine   • Speech-to-Text Audio Worker    │
├────────────────────────────────────────────────────────────────────────┤
│                       Storage & Persistence Layer                      │
│   • Crash-Resilient WAL Database      • Encrypted Credential Whitelist │
│   • Power-Fail Recovery Queue         • System Health Audit Logs       │
├────────────────────────────────────────────────────────────────────────┤
│                      Hardware Abstraction & Kernel                     │
│   • Linux LTS Kernel (ARM64)          • Rockchip NPU Driver Subsystem  │
│   • Industrial GPIO Driver (libgpiod) • Hardware Watchdog & RTC Clocks │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Core Operating Subsystems

### 3.1. Industrial Access Control & Physical I/O
SenzrOS controls physical barrier hardware via dedicated input/output channels with sub-millisecond responsiveness.

- **Non-Blocking Relay Actuation:** Actuating gates, barriers, or turnstiles runs asynchronously in background supervisors. The system holds the active relay state for a configurable duration (such as 3 seconds) and automatically disarms without blocking ongoing network or vision processing.
- **Active Safety Beam Interlock:** The operating system continuously reads optical safety sensors. If a vehicle or pedestrian obstructs the safety beam path, barrier closure is aborted immediately to prevent physical impact.
- **Acoustic and Visual Indicators:** Dedicated control lines drive multi-beep buzzer patterns for access granted, access denied, and tamper alarms, while operational LEDs indicate system health and network state.
- **Hardware Interface Redundancy:** Employs modern Linux character device GPIO drivers with automatic fallback to kernel sysfs to maintain hardware portability.

---

### 3.2. Edge Vision & NPU Acceleration Engine
SenzrOS leverages a 6.0 TOPS Tri-Core Neural Processing Unit (NPU) to run high-throughput vision pipelines without taxing CPU resources.

- **Real-Time Person Detection:** Quantized object detection models run across all three NPU cores with automatic load balancing. The system monitors camera streams, detects human presence, and manages recording lifecycles with intelligent entry and exit grace periods.
- **License Plate Recognition (ANPR):** Built specifically for vehicle access control, supporting both standard Indian RTO formats and Bharat (BH) series registrations.
  - **Headlight & Glare Filtering:** Applies adaptive contrast equalization on luminance to preserve plate legibility under harsh nighttime headlights or direct sunlight.
  - **Multi-Line Plate Support:** Dynamically classifies vehicle plate shapes to process both two-wheeler stacked plates and passenger vehicle horizontal plates.
  - **HSRP Cleanup:** Strips High Security Registration Plate artifacts, international identifiers, and non-alphanumeric noise.
  - **Positional Character Disambiguation:** Applies character validation based on character index (distinguishing letters `O`, `I`, `S`, `Z`, `B` from digits `0`, `1`, `5`, `2`, `8`).

---

### 3.3. Acoustic Processing & Embedded Transcription
- **Synchronized Audio Ingestion:** Captures ambient audio concurrently with video feeds from connected microphone inputs.
- **Embedded Speech-to-Text:** Processes audio locally on-device without cloud dependencies, providing immediate text transcripts for security audio indexing.

---

### 3.4. Offline-First Storage & Data Integrity
SenzrOS is designed for hostile power environments where sudden power cuts may occur.

- **Write-Ahead Logging (WAL):** Database operations allow simultaneous non-blocking reads and serialized atomic writes, eliminating storage lockouts during heavy logging.
- **Encrypted Local Whitelists:** RFID card hashes, authorized plate numbers, role levels, and validity schedules are stored on-device, enabling instantaneous offline entry authorization.
- **Power-Failure Recovery Queue:** Files queued for cloud dispatch are managed through an on-disk transactional ledger. If power drops mid-upload, the job resumes cleanly upon system reboot without duplicate entries or corrupted data.
- **Automatic Telemetry Lifecycle:** Historical telemetry and routine event logs are automatically purged according to retention policies to preserve local flash memory.

---

### 3.5. Secure Cloud IoT & Telemetry
- **Encrypted Communication:** All upstream events and downstream management directives travel over MQTTS protected by TLS 1.3 / TLS 1.2 with mutual certificate authentication.
- **Handshake & Device Identity:** Upon network connection, SenzrOS issues an identification handshake containing its hardware UUID, board model, firmware version, and IP configuration.
- **Real-Time Event Uplink:** Every door actuation, authorized entry, rejected attempt, and sensor alert is published upstream within milliseconds.
- **Remote Configuration:** Administrators can push configuration updates, credential updates, or remote override triggers directly to the unit.

---

### 3.6. Network Provisioning & Zero-Touch Setup
- **Standalone Wi-Fi Hotspot:** On fresh boot or upon loss of configuration, SenzrOS broadcasts an isolated setup Wi-Fi network.
- **Captive Portal Redirection:** Connecting a smartphone, tablet, or laptop immediately triggers the operating system's captive portal, redirecting the browser to the setup dashboard without requiring internet connectivity.
- **Bluetooth Low Energy (BLE) Commissioning:** Mobile maintenance applications can discover and pair with the device over BLE to transfer network credentials securely.
- **Time Synchronization:** Keeps precise system time using a multi-tier fallback NTP architecture and writes accurate time stamps directly to the battery-backed on-board Hardware Real-Time Clock (RTC).

---

### 3.7. High-Availability & Self-Healing
- **A/B Dual Partition Updates:** System updates are deployed to an inactive secondary partition. The bootloader tests the new image and commits it only after all core services successfully report healthy status.
- **Automatic Rollback:** If an update fails to boot or cannot pass self-tests, the bootloader automatically reverts to the previous working partition, preventing field bricking.
- **Hardware Watchdog Supervisor:** An active watchdog daemon continuously feeds the hardware watchdog timer. If an unrecoverable kernel panic or system hang occurs, the hardware automatically reboots into a clean state.

---

## 4. User & Management Interfaces

### 4.1. Local Web Management Dashboard
SenzrOS serves a lightweight, responsive web console directly on the local network:
- **Live System Telemetry:** Real-time gauges for CPU and NPU temperatures, RAM allocation, disk capacity, and operational uptime.
- **Hardware Pin Control:** Visual status indicators for all physical GPIO pins, manual door unlock triggers, and buzzer tests.
- **Credential Whitelist Manager:** Interface to view, add, or revoke authorized RFID cards and vehicle license plates.
- **Camera & Vision Stream:** Live preview displaying detection bounding boxes, confidence ratings, and recognized plate characters.

### 4.2. Onboard Command-Line Interface (`senzr-ctl`)
For terminal access, SSH maintenance, and automated shell scripts, SenzrOS includes a dedicated management utility:
- Inspect complete hardware serial numbers, MAC addresses, and board revisions.
- Query and toggle physical GPIO pin states.
- Dispatch timed relay pulses and buzzer sequences.
- Query active FOTA update partition status.
- Test offline credential validation rules directly from the terminal.

### 4.3. REST Integration API
SenzrOS exposes a local HTTP API on port 8000 for integration with third-party security systems, building management systems (BMS), and local access controllers:
- System status and health heartbeats.
- Dynamic runtime configuration queries and updates.
- Physical relay unlock and acoustic feedback triggers.
- Whitelist credential additions, lookups, and deletions.
- Vision pipeline start, stop, and detection query feeds.

---

## 5. Verified Hardware Benchmark Performance

During validation on the reference Orange Pi 5 Max platform (Rockchip RK3588, 8-Core ARM64, 16 GB LPDDR5):

| Performance Category | Specification / Target | Verified On-Hardware Performance |
|:---|:---|:---:|
| **NPU AI Inference Time** | Real-time object detection | **12.4 ms per frame** |
| **Video Ingestion Rate** | Full-motion camera stream | **30.0 FPS** (Zero dropped frames) |
| **Plate Recognition Processing** | MoRTH & BH syntax validation | **< 1.2 ms** per plate string |
| **Relay Pulse Accuracy** | 3000 ms non-blocking activation | **3002 ms** (Safety interlock active) |
| **Cloud MQTT Round-Trip** | Uplink event dispatch & ACK | **104.7 ms** |
| **System Memory Footprint** | Low idle overhead | **427 MB** (under 3% of available RAM) |
| **SoC Operating Temperature** | Continuous multi-worker load | **37.0 °C** (Ambient indoor operation) |
| **Crash Recovery** | Power cut during write queue | **100% queue recovered** on subsequent boot |

---

## 6. Deployment Environments & Operating Modes

- **Vehicle Access & Parking Barriers:** Automated barrier gate control driven by on-device ANPR recognition with safety beam interlock.
- **Pedestrian Turnstiles & Commercial Facilities:** High-throughput access doors and turnstiles using RFID credentials, Bluetooth mobile tokens, and person detection.
- **Remote & Off-Grid Security Points:** Solar-powered or cellular-linked security posts operating autonomously with offline credential caching and store-and-forward cloud uploads.
- **Industrial Gateways:** Edge computing appliance monitoring sensors, processing camera feeds, and forwarding telemetry to central IoT dashboards.
