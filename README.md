# Edge Gateway Framework

An edge AI security gateway that runs real-time person detection on a USB camera using the RK3588 NPU, automatically records video clips when a person is in frame, and uploads them to AWS S3 — all while providing local Wi-Fi based device provisioning through a captive-portal style web UI.

Repo: https://github.com/daltonium/edge-gateway-framework

---

## Overview

Edge Gateway Framework is a self-contained edge computing device (built on an Orange Pi 5 Max) that combines:

- On-device computer vision (YOLOv8 person detection via the RK3588 NPU)
- Automated event-based video recording and cloud upload
- A local hotspot + captive portal for zero-internet device configuration
- A dual-implementation backend (Python and C++) with a benchmark suite comparing the two
- OTA-style versioned deployment structure

The project was built as a hands-on deep dive into embedded Linux, edge AI inference, systems programming, and full-stack device management — going from bare hardware bring-up to a working, remotely configurable AI appliance.

---

## Key Features

- **Real-time person detection**: YOLOv8n model converted to `.rknn` format, running inference on the RK3588's NPU via `rknn-toolkit-lite2`.
- **Event-driven recording**: Recording starts the moment a person enters the camera frame and stops after a configurable grace period once they leave.
- **Automatic cloud upload**: Completed recordings are uploaded to an S3 bucket using `boto3`, with credentials managed via a `.env` file (excluded from version control).
- **Zero-internet provisioning**: The device broadcasts its own Wi-Fi hotspot (`hostapd` + `dnsmasq`) so it can be configured from a phone or laptop with no internet connection required.
- **Captive portal handling**: Custom `nginx` rules redirect Android/iOS/Windows captive-portal probes to the device's configuration page, with a fallback for browsers whose WebView blocks JavaScript `fetch` calls.
- **Dual backend implementation**: The core session pipeline (API server, config manager, database, session/recording pipeline) is implemented in both Python and C++, allowing direct performance comparison.
- **Benchmark suite**: `benchmark.py` and `benchmark.cpp` measure and compare pipeline performance across both implementations.
- **Lightweight web dashboard**: A vanilla HTML/CSS/JS frontend served via `nginx`, showing live device status (hostname, IP, config state, camera connection) and allowing configuration changes.
- **Versioned OTA-style deployments**: Release archives (`v1.0.0.zip`, `v1.0.1.zip`) and a `current/` symlinked directory structure support safe, rollback-friendly updates.

---

## Architecture

```
                     ┌─────────────────────────┐
                     │        USB Camera        │
                     └────────────┬─────────────┘
                                  │
                          ┌───────▼────────┐
                          │  RK3588 NPU     │
                          │  YOLOv8n (rknn) │
                          │  Person Detect  │
                          └───────┬────────┘
                                  │ person detected/left
                          ┌───────▼────────┐
                          │ Recorder /      │
                          │ Session Pipeline│
                          └───────┬────────┘
                                  │ completed clip
                          ┌───────▼────────┐
                          │ S3 Uploader     │
                          │ (boto3)         │
                          └────────────────┘

  ┌────────────┐   Wi-Fi Hotspot   ┌────────────────────┐
  │  Phone /   │◄──────────────────┤ hostapd + dnsmasq   │
  │  Laptop    │                   └─────────┬──────────┘
  └─────┬──────┘                              │
        │ HTTP (captive portal / browser)     │
        ▼                                     ▼
  ┌─────────────────────────────────────────────────┐
  │                    nginx                          │
  │  - Serves static frontend (HTML/CSS/JS)           │
  │  - Reverse-proxies /api/* to backend               │
  │  - Redirects OS captive-portal probes              │
  └───────────────────────┬────────────────────────────┘
                           │
                  ┌────────▼─────────┐
                  │  Backend API      │
                  │ (Python / C++)    │
                  │ - Config Manager  │
                  │ - Database (SQLite)│
                  │ - Logger Utils     │
                  │ - Session Pipeline │
                  └───────────────────┘
```

---

## Tech Stack

| Layer | Technology |
|---|---|
| Edge AI inference | YOLOv8n, RKNN Toolkit Lite2, RK3588 NPU |
| Camera / CV | OpenCV (headless), NumPy |
| Backend (Python) | FastAPI-style service, SQLite, boto3, python-dotenv |
| Backend (C++) | httplib, nlohmann/json, SQLite3, OpenSSL |
| Networking | hostapd, dnsmasq, nginx |
| Cloud | AWS S3 |
| Frontend | HTML, CSS, vanilla JavaScript |
| Hardware | Orange Pi 5 Max (RK3588), USB Camera |
| Build tools | CMake (C++), pip/venv (Python) |

---

## Repository Structure

```
edge-gateway-framework/
├── cpp/                          # C++ backend implementation
│   ├── include/app/               # Headers: ApiServer, ConfigManager, Database, LoggerUtils, Models
│   ├── include/sessionpipeline/   # Headers: AudioCapture, PersonDetection, Recorder, Session, Transcriber, Uploader
│   ├── src/app/                   # Source files mirroring headers
│   ├── src/sessionpipeline/
│   ├── thirdparty/httplib.h
│   ├── CMakeLists.txt
│   └── benchmark.cpp
├── current/                       # Active deployed version (OTA structure)
│   ├── backend/
│   │   ├── database/config.json
│   │   ├── models/yolov8.rknn
│   │   ├── sessionpipeline/       # audiocapture, persondetection, recordermanager, session, transcriber, uploader, etc.
│   │   ├── main.py
│   │   ├── cameradetector.py
│   │   ├── s3uploader.py
│   │   └── requirements.txt
│   ├── frontend/                  # app.js, index.html, style.css
│   └── scripts/                   # start-gateway.sh, stop-gateway.sh
├── downloads/                      # Versioned release archives (v1.0.0.zip, v1.0.1.zip)
├── scripts/                         # Top-level start/stop scripts
├── benchmark.py / benchmark.cpp / benchmark.txt / cppbenchmark.txt
├── updater.py
├── .gitignore
└── README.md
```

---

## How It Works

1. **Provisioning**: On first boot, the device broadcasts a Wi-Fi hotspot. Connecting to it triggers a captive portal (or manual browser visit to `http://192.168.4.1`) that loads the configuration dashboard.
2. **Configuration**: Users set the device name, mode, Wi-Fi credentials, and provisioning options through the web form, which POSTs to `/api/config`.
3. **Detection loop**: Once configured, the camera pipeline continuously captures frames, runs them through the YOLOv8n model on the NPU, and checks for a person (COCO class 0) above a confidence threshold.
4. **Recording**: The moment a person is detected, recording starts. Recording continues until the person has been absent for a defined grace period (e.g. 3 seconds), then the clip is finalized.
5. **Upload**: Finalized clips are uploaded asynchronously to an S3 bucket via a background thread, keeping the detection loop uninterrupted.
6. **Monitoring**: The `/api/status` endpoint reports live device health (online status, hostname, IP, config state, camera connection, pipeline running state) to the dashboard, polled every few seconds.

---

## Notable Engineering Challenges Solved

- **Static asset misrouting**: Diagnosed and fixed an nginx configuration issue where CSS/JS requests were falling through to `index.html` due to a missing dedicated `/static/` location block.
- **Captive portal WebView fetch blocking**: Identified that Android's system WebView (used for captive portal popups) silently blocks `fetch`/`XMLHttpRequest` calls, causing the dashboard to appear stuck on "Loading...". Solved with OS-specific captive-portal probe redirects (Android `generate_204`, Apple `hotspot-detect.html`, Windows `connecttest.txt`/`ncsi.txt`) that push users into the full Chrome browser, plus a timeout-based fetch fallback in the frontend.
- **Cross-language pipeline parity**: Maintained functionally equivalent session pipelines in both Python and C++ to benchmark real-world performance differences for the same NPU-driven workload.
- **Safe OTA structure**: Adopted a versioned `current/` + archived `downloads/` layout to support rollback-friendly updates without breaking an active deployment.

---

## Setup & Linux Deployment

> 📖 **Complete Linux Guide:** For full step-by-step installation, dependency compilation, camera/NPU configuration, systemd services, and production deployment, refer to the [**Linux Setup & Guide Steps**](file:///f:/PI5MAX/LINUX_SETUP_GUIDE.md).
>
> 🚀 **Quick SSH Build Guide:** For quick on-board SSH compile and run instructions, see [**BUILD_INSTRUCTIONS.md**](file:///f:/PI5MAX/BUILD_INSTRUCTIONS.md).

### High-Level Setup Summary:

1. Flash Orange Pi 5 Max with a supported Linux image.
2. Install Python 3.10 (required for `rknn-toolkit-lite2` compatibility) via `altinstall`, alongside the system Python.
3. Install the RKNN NPU runtime (`librknnrt.so`) and the `rknn-toolkit-lite2` Python wheel matching your Python/ABI version.
4. Install OpenCV (headless), NumPy, boto3, and python-dotenv in a dedicated virtual environment.
5. Connect and verify the USB camera (`/dev/video0`).
6. Download or convert a YOLOv8n `.rknn` model (pre-converted models available via Rockchip's `rknn-model-zoo`).
7. Configure `hostapd`, `dnsmasq`, and `nginx` for hotspot provisioning and captive portal handling.
8. Set AWS credentials in a local `.env` file (never committed to git).
9. Build the C++ backend with CMake, or run the Python backend directly.
10. Start the gateway using `scripts/start-gateway.sh`.

---

## Security Notes

- AWS credentials are loaded from an untracked `.env` file and must never be committed to version control.
- Rotate AWS keys immediately if they were ever exposed during development.
- The device's Wi-Fi hotspot is intended for local provisioning only and does not provide internet passthrough by design.

---

## Roadmap / Future Work

- Captive portal auto-popup refinement across more device/OS combinations
- Optional internet passthrough mode
- Expanded AI capabilities (multi-class detection, face recognition modes)
- Formal versioned release automation via `updater.py`

---

## Author

Built as an independent embedded systems + edge AI project, covering hardware bring-up, NPU-accelerated inference, systems programming (Python & C++), networking, and cloud integration end-to-end.
