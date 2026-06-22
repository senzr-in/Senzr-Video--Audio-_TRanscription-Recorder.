# Edge Gateway Framework

> **Version:** v2.0.0  
> **Last Updated:** June 2026  
> **Target Device:** Orange Pi 5 Max (Debian Bookworm Server)  
> **Development Environment:** Ubuntu VM (VirtualBox) on Windows 11  
> **Access Method:** ADB shell — no monitor, no keyboard, no Ethernet required  

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Hardware & Development Setup](#hardware--development-setup)
3. [System Architecture](#system-architecture)
4. [Repository Structure](#repository-structure)
5. [Backend — Control Plane](#backend--control-plane)
6. [Session Pipeline — AI Layer](#session-pipeline--ai-layer)
7. [Detection Engine](#detection-engine)
8. [Audio Capture & Sampling](#audio-capture--sampling)
9. [Queue & Thread Model](#queue--thread-model)
10. [Video FPS & Recording](#video-fps--recording)
11. [Frontend — Camera Control Dashboard](#frontend--camera-control-dashboard)
12. [Database & Config](#database--config)
13. [API Reference](#api-reference)
14. [Systemd & Service Setup](#systemd--service-setup)
15. [Network Stack](#network-stack)
16. [AWS S3 Upload](#aws-s3-upload)
17. [Known Issues & Debug History](#known-issues--debug-history)
18. [Upgrade Notes for AI Agents](#upgrade-notes-for-ai-agents)

---

## Project Overview

Edge Gateway Framework is a fully offline-capable edge AI appliance running on an Orange Pi 5 Max. It provides:

- A local Wi-Fi hotspot and captive portal for device control.
- A FastAPI control plane serving a mobile-friendly web UI.
- A YOLOv8n-based person detection pipeline running on the device's RK3588 NPU.
- Per-session video recording (MP4) and audio recording (WAV) triggered by detection.
- Automatic speech-to-text transcription of session audio.
- AWS S3 upload of all session files (video, audio, transcript).

The design principle is: **no cloud dependency for control or setup**. Everything that detects, records, transcribes, and uploads is orchestrated locally on the device.

---

## Hardware & Development Setup

| Component | Detail |
|---|---|
| Device | Orange Pi 5 Max |
| SoC | Rockchip RK3588 (NPU: 6 TOPS) |
| OS | Debian Bookworm Server (headless) |
| Dev host | Windows 11 laptop |
| VM | Ubuntu (VirtualBox) — main coding environment |
| IDE | VS Code inside the Ubuntu VM |
| Device access | ADB shell (USB) |
| Source control | GitHub |
| Deploy path | Pull from GitHub on the Orange Pi |

### Development Workflow

```
Code in VS Code (Ubuntu VM)
        ↓
git commit + push to GitHub
        ↓
adb shell → cd /opt/edge-gateway → git pull
        ↓
sudo systemctl restart edge-gateway
```

The Orange Pi is the **deployed runtime**. The VM is the safe **development workspace**. Never develop directly on the device.

---

## System Architecture

The project is organized as three layers:

```
Layer 3: FastAPI app + Web UI (Python, HTML/CSS/JS)
              ↕  REST API calls only
Layer 2: Linux services (hostapd, dnsmasq, systemd, nginx)
              ↕  systemd service units
Layer 1: Debian Linux on Orange Pi 5 Max
```

**Rule:** The web UI never touches Linux directly. All control goes through FastAPI endpoints. FastAPI owns config, state, and service orchestration.

### Data Flow (per session)

```
Camera (V4L2)
    ↓ frames
PersonDetector (YOLOv8n on NPU)
    ↓ person seen for 5 consecutive frames → START
SessionManager  ←─────────────────────────────────┐
    ↓                                              │
VideoWriter (MP4 @ 20 FPS)                        │
AudioRecorder (WAV, 16kHz, mono, 16-bit)          │
    ↓ person absent for 40 consecutive frames → STOP
Transcriber (speech-to-text via whisper)
    ↓
S3Uploader (video + audio + transcript JSON → AWS S3)
```

---

## Repository Structure

```
edge-gateway-framework/
├── current/
│   ├── backend/
│   │   ├── database/
│   │   │   └── config.json              # Device config (JSON)
│   │   ├── models/
│   │   │   ├── yolov8.rknn              # YOLOv8 RKNN model (primary)
│   │   │   ├── yolov8n.rknn             # YOLOv8n RKNN model (active)
│   │   │   └── yolov8n.onnx             # ONNX export (reference)
│   │   ├── scripts/
│   │   │   ├── start-gateway.sh         # Startup: assigns IP, starts services
│   │   │   └── stop-gateway.sh          # Graceful shutdown
│   │   ├── session_pipeline/            # AI session pipeline (modular)
│   │   │   ├── __init__.py
│   │   │   ├── audio_capture.py         # AudioRecorder class (PyAudio/ALSA)
│   │   │   ├── config.py                # Pipeline constants
│   │   │   ├── models.py                # Pydantic models for pipeline
│   │   │   ├── person_detection.py      # Detection logic (separate module)
│   │   │   ├── queues.py                # Shared queue definitions
│   │   │   ├── recorder_manager.py      # Manages video+audio recording
│   │   │   ├── run_session_pipeline.py  # Pipeline entry point
│   │   │   ├── session_manager.py       # Session folder/path management
│   │   │   ├── transcriber.py           # Whisper-based transcription
│   │   │   ├── uploader.py              # S3 upload for sessions
│   │   │   ├── video_capture.py         # OpenCV video capture wrapper
│   │   │   ├── test_audio.py
│   │   │   ├── test_session.py
│   │   │   └── test_transcribe.py
│   │   ├── camera_detection.py          # Standalone detection (older)
│   │   ├── camera_detector.py           # Full detector (current, with audio+upload)
│   │   ├── camera_detector.py.pre_audio # Pre-audio backup
│   │   ├── camera_service.py            # Entry point for camera as a service
│   │   ├── config_manager.py            # Config read/write (snake_case version)
│   │   ├── configmanager.py             # Config read/write (camelCase version)
│   │   ├── database.py                  # SQLite init + config change logging
│   │   ├── logger_utils.py              # Log utils (snake_case version)
│   │   ├── loggerutils.py               # Log utils (camelCase version)
│   │   ├── main.py                      # FastAPI app (primary entry point)
│   │   ├── models.py                    # Pydantic models (API layer)
│   │   ├── run_camera_detector.py       # CLI runner for detector
│   │   ├── run_session_pipeline_main.py # CLI runner for full pipeline
│   │   ├── s3_uploader.py               # S3 upload helper
│   │   └── alsa_controls.txt            # ALSA mixer state snapshot
│   ├── database/
│   │   └── config.json                  # Top-level config copy
│   ├── frontend/
│   │   ├── index.html                   # Dashboard HTML
│   │   ├── app.js                       # Dashboard JS (polling, controls)
│   │   └── style.css                    # Dashboard CSS (palette: #F4E1C1 + #008080)
│   ├── models/
│   │   └── yolov5s_relu.rknn            # Older YOLOv5s model (reference)
│   └── requirements.txt
├── downloads/
│   ├── v1.0.0.zip
│   └── v1.0.1.zip
├── pi-local-backup/
│   ├── camera_detector.py               # Pi-side backup
│   └── s3_uploader.py
├── scripts/
│   ├── start-gateway.sh
│   └── stop-gateway.sh
├── updater.py                           # OTA updater script
├── .gitignore
└── README.md
```

### File Naming Note

Several files exist in both `snake_case` and `camelCase` variants (`config_manager.py` vs `configmanager.py`, `logger_utils.py` vs `loggerutils.py`). This is a refactor artifact. The `camelCase` versions are older and used by `main.py` (via imports from `.configmanager`, `.loggerutils`). Do not delete either until imports are fully unified.

---

## Backend — Control Plane

**Entry point:** `current/backend/main.py`  
**Framework:** FastAPI  
**Runtime location on device:** `/opt/edge-gateway/`  
**Python venv:** `/opt/edge-gateway/venv`

### Startup sequence

1. `initdb()` — creates SQLite tables if not present.
2. Mounts frontend directory as `/static`.
3. Tries to import `PersonDetector` from `camera_detector.py`. If RKNN is unavailable (not on device), import fails silently and the camera endpoints return a "not available" message.
4. `CameraManager` singleton is created. Camera starts **OFF** by default.

### CameraManager

`CameraManager` is the thread orchestrator for the camera pipeline. It is a singleton created at module load time.

| Property | Description |
|---|---|
| `camera_enabled` | True if camera has been turned ON via API |
| `detector_running` | True if the detector thread is alive |
| `is_recording` | True if a session is actively being recorded |
| `current_recording_file` | Path to the currently recording MP4 |
| `last_recorded_file` | Path to the last completed MP4 |

**Turn ON:** Creates a `PersonDetector`, creates a `threading.Event` stop signal, starts the detector in a daemon thread named `camera-detector`.

**Turn OFF:** Sets the stop event, joins the thread (timeout 10s), cleans up detector and thread references.

---

## Session Pipeline — AI Layer

The `session_pipeline/` package is a self-contained AI session management system. It is designed to be importable independently from the FastAPI app for testing.

### Modules

| File | Role |
|---|---|
| `config.py` | Pipeline-wide constants (FPS, resolution, thresholds, audio params) |
| `queues.py` | Shared `queue.Queue` instances used between threads |
| `video_capture.py` | OpenCV V4L2 capture wrapper |
| `audio_capture.py` | PyAudio ALSA capture, writes WAV |
| `person_detection.py` | YOLOv8n RKNN inference, detection logic |
| `recorder_manager.py` | Controls VideoWriter and AudioRecorder per session |
| `session_manager.py` | Creates session directories, provides file paths |
| `transcriber.py` | Calls Whisper to transcribe session WAV, writes transcript JSON |
| `uploader.py` | Uploads session folder (video + audio + transcript) to S3 |
| `run_session_pipeline.py` | Wires all modules together, starts all threads |

---

## Detection Engine

**Model:** YOLOv8n converted to RKNN format  
**Model path:** `current/backend/models/yolov8n.rknn`  
**NPU runtime:** `rknnlite` (RK3588 NPU, `NPU_CORE_AUTO`)  
**Input size:** 640×640 (letterboxed from 640×480 capture)

### Detection constants

| Constant | Value | Meaning |
|---|---|---|
| `PERSON_CLASS_ID` | 0 | COCO class 0 = person |
| `OBJ_THRESH` | 0.30 | Minimum score to count as a detection |
| `EARLY_OBJ_THRESH` | 0.15 | Pre-filter: skip cells below this objectness |
| `NMS_THRESH` | 0.45 | Non-max suppression overlap threshold |
| `START_CONFIRM_FRAMES` | 5 | Consecutive frames with person required to START recording |
| `STOP_CONFIRM_FRAMES` | 40 | Consecutive frames WITHOUT person required to STOP recording |
| `DEBUG_EVERY_N_FRAMES` | 10 | Print debug log every N frames |

### Decode pipeline

YOLOv8n produces 9 RKNN output tensors (3 scales × 3 outputs each: box map, class map, score map). The custom decoder:

1. Applies DFL (Distribution Focal Loss) decoding on box tensors.
2. Applies sigmoid to class and score maps.
3. Decodes bounding box coordinates from anchor-free grid.
4. Filters by `OBJ_THRESH`.
5. Runs `cv2.dnn.NMSBoxes` for non-max suppression.

Person score is computed as `max(person_cls, person_cls * obj_score)` to improve sensitivity with the RKNN quantized model.

---

## Audio Capture & Sampling

**Library:** PyAudio (ALSA backend)  
**Format:** WAV  
**Sample rate:** 16,000 Hz (16 kHz)  
**Channels:** 1 (mono)  
**Bit depth:** 16-bit PCM (`paInt16`)  
**Chunk size:** 1024 samples per read (≈ 64ms per chunk at 16kHz)

### Why these values

- **16 kHz mono** is the standard input format for speech recognition models including Whisper. Higher rates waste storage and processing without improving transcription accuracy for speech.
- **1024 chunk size** is a safe default that balances capture latency against CPU overhead.
- **16-bit PCM** is the minimum lossless format Whisper accepts.

### AudioRecorder

`AudioRecorder` runs in its own daemon thread. It opens the ALSA input device, writes chunks to a WAV file for the duration of the session, and stops cleanly when `stop()` is called. The WAV file is passed to `transcriber.py` after the session ends.

### ALSA device note

The Orange Pi 5 Max has multiple ALSA cards. The correct input device index must match the USB microphone or onboard audio. The `alsa_controls.txt` file captures the ALSA mixer state for reference. If audio capture fails, check the device index in `audio_capture.py` config.

---

## Queue & Thread Model

### Philosophy

Each processing stage runs in its own thread. Queues decouple the stages so a slow stage (e.g., transcription) does not block a fast stage (e.g., camera capture). This is critical on an embedded device where inference, I/O, and network upload all have wildly different latencies.

### Thread map

| Thread name | Owned by | Responsibility |
|---|---|---|
| `camera-detector` | `CameraManager` | Runs the full detection + record loop |
| `audio-capture` | `AudioRecorder` | Captures audio into WAV while session is active |
| `s3-upload-{path}` | `PersonDetector._stop_recording` | Uploads a completed session file to S3 (daemon, fire-and-forget) |

### Queue design (session_pipeline)

Queues are defined in `queues.py` and shared across modules as module-level singletons. This avoids passing queue references through constructors.

| Queue | Producer | Consumer | Contents |
|---|---|---|---|
| Frame queue | `video_capture` thread | `person_detection` thread | Raw OpenCV frames |
| Event queue | `person_detection` thread | `recorder_manager` thread | `SESSION_START` / `SESSION_STOP` events |
| Transcription queue | `recorder_manager` thread | `transcriber` thread | Finished session paths |
| Upload queue | `transcriber` thread | `uploader` thread | Session folders ready for S3 |

### Thread safety

All shared state inside `PersonDetector` and `CameraManager` is protected by `threading.Lock()`. Properties (`is_recording`, `current_recording_file`, `last_recorded_file`) acquire the lock before returning. This is important because the FastAPI polling endpoint reads these properties from the main thread while the detector thread writes them.

---

## Video FPS & Recording

**Capture format:** MJPEG (V4L2, `cv2.CAP_V4L2`)  
**Capture resolution:** 640×480  
**Recording FPS:** 20.0 FPS (hardcoded in `cv2.VideoWriter`)  
**Video codec:** `mp4v` (MPEG-4 Part 2 via OpenCV)  
**Output container:** `.mp4`  
**Output path:** `/opt/edge-gateway/recordings/` → session subfolder

### Why 20 FPS

20 FPS is a balance between:
- File size (lower FPS = smaller files on flash storage).
- Smoothness (20 FPS is acceptable for surveillance/review purposes).
- CPU headroom (each frame runs NPU inference; 20 FPS leaves margin for audio capture and API serving to run concurrently).

### Session naming

`SessionManager` creates a unique session ID (typically a timestamp string) and a session directory:

```
/opt/edge-gateway/recordings/
└── session_20260622_152301/
    ├── video.mp4
    ├── audio.wav
    └── transcript.json
```

---

## Frontend — Camera Control Dashboard

**Files:** `current/frontend/index.html`, `app.js`, `style.css`  
**Version:** v2.0.0  
**Served at:** `/` (FastAPI serves `index.html`) and `/static/` (CSS + JS)

### Color palette

| Token | Value | Used for |
|---|---|---|
| Primary teal | `#008080` | Logo, headings icon, version badge, buttons, ON state |
| Warm cream | `#F4E1C1` | All body text, button-on text, file display |
| Background | `#1b1712` | Page background (warm dark) |
| Surface | `#241f18` | Card background |
| Surface-2 | `#2c261e` | Status item background |
| Recording red | `#ff6b6b` | Recording status, error messages |

### Dashboard sections

1. **Device Status card** — shows `status`, `hostname`, `ip_address` from `/api/status`. Uses a loading shimmer on first load.
2. **Detection Settings card** — shows camera ON/OFF state, recording status, recording dot indicator, and current/last recorded file path.

### Polling

`app.js` polls `/api/camera/status` every **3 seconds** using `setInterval`. This keeps the recording state, recording indicator dot, and filename up to date without requiring a page refresh.

### Rules enforced in JS

- All `fetch()` calls use same-origin relative paths only (no hardcoded `127.0.0.1`). This is essential because the device is accessed over Wi-Fi at `192.168.4.1`.
- ON and OFF buttons are disabled while a request is in-flight (prevents double-click race conditions).
- Silent failure on poll errors (no user-visible error on every 3-second tick).

---

## Database & Config

### JSON Config

**Path:** `current/backend/database/config.json`

```json
{
  "devicename": "edge-gateway-framework",
  "mode": "object",
  "wifissid": "EdgeGateway",
  "wifipassword": "changeme123",
  "provisioningenabled": true,
  "cameraconnected": false
}
```

This is the user-facing device configuration. It is read and written by `configmanager.py`. Changes are also logged to SQLite.

### SQLite

**Path:** `current/backend/database/edgegateway.db`  
**Created by:** `database.py → initdb()`

| Table | Purpose |
|---|---|
| `systeminfo` | Key-value store for system metadata |
| `configchanges` | Audit log of every config change (timestamp, IP, MAC, user agent, old config, new config) |

MAC address is looked up from the dnsmasq DHCP lease file at `/var/lib/misc/dnsmasq.leases` to correlate a config change with a physical device.

---

## API Reference

All endpoints are served by FastAPI at `http://192.168.4.1` (on the device) or `http://localhost:8000` (in dev).

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Serves `index.html` (the dashboard) |
| `GET` | `/api/status` | Returns device status, hostname, IP |
| `GET` | `/api/config` | Returns current JSON config |
| `POST` | `/api/config` | Updates JSON config, logs to SQLite |
| `GET` | `/api/camera/status` | Returns camera state, recording state, file paths |
| `POST` | `/api/camera/on` | Starts the person detector thread |
| `POST` | `/api/camera/off` | Stops the person detector thread |

### `/api/camera/status` response

```json
{
  "camera_enabled": true,
  "detector_running": true,
  "is_recording": false,
  "current_recording_file": null,
  "last_recorded_file": "/opt/edge-gateway/recordings/session_20260622_152301/video.mp4"
}
```

---

## Systemd & Service Setup

The application runs as a `systemd` service on the Orange Pi.

### Service file location

`/etc/systemd/system/edge-gateway.service`

### Key requirements

- The service must run **after network-online.target** so the API is available once networking is up.
- The startup script (`start-gateway.sh`) must assign `192.168.4.1/24` to `wlan0` **before** starting `dnsmasq`, because `ip addr add` is not persistent and dnsmasq will fail if the interface has no address at DHCP start time.
- The venv must be activated: `ExecStart=/opt/edge-gateway/venv/bin/uvicorn current.backend.main:app --host 0.0.0.0 --port 8000`

### systemd watchdog

A systemd watchdog is configured so the service auto-restarts if uvicorn crashes or hangs.

---

## Network Stack

| Component | Role |
|---|---|
| `hostapd` | Creates the Wi-Fi access point (SSID: `EdgeGateway`) |
| `dnsmasq` | Provides DHCP for connected clients, DNS redirect for captive portal |
| `nginx` | Reverse proxy: port 80 → uvicorn port 8000 |
| `wlan0` | Static IP: `192.168.4.1/24` |
| DHCP range | `192.168.4.10` – `192.168.4.50` |

Clients connect to the `EdgeGateway` Wi-Fi, get a DHCP address in the `192.168.4.x` range, and their browser is redirected to `192.168.4.1` (the dashboard).

---

## AWS S3 Upload

**Module:** `current/backend/s3_uploader.py` and `session_pipeline/uploader.py`  
**SDK:** `boto3`

Session files (video, audio, transcript) are uploaded to S3 after each session ends. Upload runs in a daemon thread so it does not block the main detection loop.

### S3 credentials

Stored in `AWS-info.txt` (not committed to Git). Must be configured as environment variables or in `~/.aws/credentials` on the device:

```
AWS_ACCESS_KEY_ID=...
AWS_SECRET_ACCESS_KEY=...
AWS_DEFAULT_REGION=...
S3_BUCKET_NAME=...
```

### Upload path structure in S3

```
s3://your-bucket/sessions/{session_id}/video.mp4
s3://your-bucket/sessions/{session_id}/audio.wav
s3://your-bucket/sessions/{session_id}/transcript.json
```

---

## Known Issues & Debug History

### 1. RKNN model output mismatch

**Problem:** YOLOv8n RKNN model produced 9 output tensors instead of the expected 6. The post-processing code assumed the standard YOLOv8 head layout.  
**Fix:** Custom `decode_yolov8_rknn()` function handles 9-output layout (3 scales × 3 tensors: box map, class map, score map). DFL decoding applied per branch.

### 2. Low detection sensitivity

**Problem:** `person_cls * obj_score` scoring was too conservative with the quantized RKNN model, missing detections at moderate confidence.  
**Fix:** Changed to `max(person_cls, person_cls * obj_score)` to avoid penalizing detections where the objectness score is lower than expected from quantization.

### 3. dnsmasq fails on boot

**Problem:** `dnsmasq` starts before `wlan0` has its static IP assigned, so DHCP has no address to serve from.  
**Fix:** `start-gateway.sh` runs `ip addr add 192.168.4.1/24 dev wlan0` before starting `dnsmasq`. This must happen every boot since `ip addr add` is not persistent.

### 4. Audio not capturing

**Problem:** ALSA device index mismatch between USB mic and onboard audio.  
**Fix:** Use `arecord -l` on the device to list capture hardware. Set the correct device index in `audio_capture.py`. The `alsa_controls.txt` snapshot is provided for reference.

### 5. Empty transcript on silent sessions

**Problem:** Whisper raises an error or returns garbage on a WAV file with no speech.  
**Fix:** Transcriber catches exceptions and writes an empty transcript JSON `{"text": "", "segments": []}` so the session upload still completes cleanly.

### 6. CSS palette not applying

**Problem:** Mobile browser cached older stylesheet after CSS update.  
**Fix:** Hard-refresh the browser (hold reload or clear cache) after pushing a new `style.css` to the device. The CSS palette uses `#F4E1C1` and `#008080` throughout all token definitions including `--color-text`, `--color-primary`, button colors, and borders.

---

## Upgrade Notes for AI Agents

This section is specifically for any AI agent picking up future development on this project.

### What is stable and must not be broken

- The relative `fetch()` paths in `app.js` — never hardcode `127.0.0.1` or any IP.
- The `CameraManager` singleton and its lock model in `main.py`.
- The `START_CONFIRM_FRAMES = 5` and `STOP_CONFIRM_FRAMES = 40` values — they are tuned for the hardware and network latency.
- The 9-output RKNN decode logic — do not replace with a standard 6-output decoder.
- Audio sample rate 16 kHz, mono, 16-bit — required by Whisper.
- The session directory structure — S3 paths depend on it.

### What is intentionally duplicated

- `configmanager.py` vs `config_manager.py` — both exist, `main.py` uses the camelCase one.
- `loggerutils.py` vs `logger_utils.py` — same reason.
- Do not delete either until imports in `main.py` are unified.

### What is next / planned

- Unify naming conventions across all backend modules (snake_case throughout).
- Add a session history endpoint (`/api/sessions`) to list past recordings.
- Add a live detection confidence display to the dashboard.
- Consider replacing `mp4v` codec with `h264` for smaller file sizes.
- Add a config UI to the dashboard for SSID/password changes.
- Clean up legacy files: `camera_detection.py`, `camera_detector.py.pre_audio`.

### Key file to always update when changing pipeline

If you change detection thresholds, FPS, audio settings, or queue sizes, update **both** `current/backend/camera_detector.py` **and** `current/backend/session_pipeline/config.py` to keep them in sync.

---

*This README is the single source of truth for the project as of v2.0.0. Keep it updated with every significant change.*
