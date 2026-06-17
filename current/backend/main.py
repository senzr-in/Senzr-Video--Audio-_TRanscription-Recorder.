from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pathlib import Path
import socket
import logging
import threading

from backend.config_manager import read_config, write_config
from backend.database import init_db, log_config_change
from backend.models import ConfigModel
from backend.logger_utils import get_timestamp, diff_configs, write_file_log, get_mac_from_ip

# ── Optional: import s3 uploader if AWS is configured ─────────────────────────
try:
    from backend.s3_uploader import upload_recording as s3_upload_fn
except Exception:
    s3_upload_fn = None

# ── Optional: import PersonDetector (fails gracefully on non-Pi hardware) ─────
try:
    from backend.camera_detector import PersonDetector
    _DETECTOR_AVAILABLE = True
except Exception as _det_import_err:
    PersonDetector = None
    _DETECTOR_AVAILABLE = False

import json

log = logging.getLogger("edge-gateway")
logging.basicConfig(level=logging.INFO)

FRONTEND_DIR = Path(__file__).parent.parent / "frontend"
INDEX_FILE   = FRONTEND_DIR / "index.html"

app = FastAPI(title="Edge Gateway Framework API")

if FRONTEND_DIR.exists():
    try:
        app.mount("/static", StaticFiles(directory=str(FRONTEND_DIR)), name="static")
    except Exception as e:
        log.error(f"Could not mount static files: {e}")


# ─────────────────────────────────────────────────────────────────────────────
# CameraManager — single source of truth for camera lifecycle
# ─────────────────────────────────────────────────────────────────────────────

class CameraManager:
    """
    Manages the PersonDetector thread lifecycle.

    Thread-safety contract:
      All public methods acquire self._lock before mutating state.
      Properties are read-only snapshots; they briefly acquire _lock.
    """

    def __init__(self):
        self._lock = threading.Lock()
        self._detector: "PersonDetector | None" = None
        self._thread:   threading.Thread | None = None
        self._stop_event: threading.Event | None = None
        self._camera_enabled = False

    # ── public properties (safe to call from FastAPI route handlers) ──────

    @property
    def camera_enabled(self) -> bool:
        with self._lock:
            return self._camera_enabled

    @property
    def detector_running(self) -> bool:
        with self._lock:
            return self._thread is not None and self._thread.is_alive()

    @property
    def is_recording(self) -> bool:
        with self._lock:
            if self._detector is None:
                return False
            return self._detector.is_recording

    @property
    def current_recording_file(self) -> "str | None":
        with self._lock:
            if self._detector is None:
                return None
            return self._detector.current_recording_file

    @property
    def last_recorded_file(self) -> "str | None":
        with self._lock:
            if self._detector is None:
                return None
            return self._detector.last_recorded_file

    # ── lifecycle methods ─────────────────────────────────────────────────

    def turn_on(self) -> dict:
        """Start the detector thread. Idempotent — safe to call when already ON."""
        with self._lock:
            if self._camera_enabled and self._thread is not None and self._thread.is_alive():
                return {"ok": True, "message": "Camera already ON"}

            if not _DETECTOR_AVAILABLE:
                return {"ok": False, "message": "Camera detector not available on this hardware"}

            # Ensure previous thread is cleaned up before starting a new one
            if self._thread is not None and not self._thread.is_alive():
                self._thread = None
                self._detector = None
                self._stop_event = None

            try:
                self._stop_event = threading.Event()
                self._detector = PersonDetector(s3_uploader=s3_upload_fn)
                self._thread = threading.Thread(
                    target=self._detector.run,
                    args=(self._stop_event,),
                    daemon=True,
                    name="camera-detector"
                )
                self._thread.start()
                self._camera_enabled = True
                log.info("[CameraManager] Camera turned ON, detector thread started")
                return {"ok": True, "message": "Camera turned ON"}
            except Exception as e:
                log.error(f"[CameraManager] Failed to start detector: {e}")
                self._camera_enabled = False
                self._detector = None
                self._thread = None
                self._stop_event = None
                return {"ok": False, "message": f"Failed to start camera: {e}"}

    def turn_off(self) -> dict:
        """Stop the detector thread cleanly. Idempotent — safe to call when already OFF."""
        with self._lock:
            if not self._camera_enabled and self._thread is None:
                return {"ok": True, "message": "Camera already OFF"}

            self._camera_enabled = False

            if self._stop_event is not None:
                self._stop_event.set()

            # Release lock while we wait for the thread — prevents deadlock
            stop_event_ref = self._stop_event
            thread_ref     = self._thread

        if thread_ref is not None:
            thread_ref.join(timeout=10)

        with self._lock:
            self._thread = None
            self._stop_event = None
            # Keep _detector reference only for its last_recorded_file property
            # It has already released its camera and RKNN resources inside run()
            # We'll null it out so we can create a fresh instance on next turn_on()
            self._detector = None

        log.info("[CameraManager] Camera turned OFF, detector thread stopped")
        return {"ok": True, "message": "Camera turned OFF"}

    def get_status(self) -> dict:
        """Return a JSON-serialisable status snapshot."""
        rec_file = self.current_recording_file
        last_file = self.last_recorded_file
        return {
            "camera_enabled":        self.camera_enabled,
            "detector_running":      self.detector_running,
            "is_recording":          self.is_recording,
            "current_recording_file": rec_file,
            "last_recorded_file":    last_file,
        }


# Global singleton — created once at module load, used by all route handlers
camera_manager = CameraManager()


# ─────────────────────────────────────────────────────────────────────────────
# FastAPI lifecycle
# ─────────────────────────────────────────────────────────────────────────────

@app.on_event("startup")
def startup_event():
    init_db()
    log.info("Edge Gateway started. Backend ready. Camera is OFF by default.")


# ─────────────────────────────────────────────────────────────────────────────
# Frontend serving
# ─────────────────────────────────────────────────────────────────────────────

@app.api_route("/", methods=["GET", "HEAD"], response_class=HTMLResponse)
def serve_index():
    try:
        if not INDEX_FILE.exists():
            raise FileNotFoundError(f"{INDEX_FILE} not found")
        content = INDEX_FILE.read_text(encoding="utf-8")
        return HTMLResponse(content=content)
    except FileNotFoundError as e:
        log.error(f"index.html missing: {e}")
        return HTMLResponse(
            content="<h2>Gateway is running — UI file missing. Update in progress or contact admin.</h2>",
            status_code=200
        )
    except Exception as e:
        log.error(f"Error serving index.html: {e}")
        return HTMLResponse(
            content="<h2>Gateway is running — UI temporarily unavailable.</h2>",
            status_code=200
        )


# ─────────────────────────────────────────────────────────────────────────────
# Device status (unchanged)
# ─────────────────────────────────────────────────────────────────────────────

@app.get("/api/status")
def get_status():
    try:
        hostname = socket.gethostname()
        try:
            ip_address = socket.gethostbyname(hostname)
        except Exception:
            ip_address = "unknown"
        return {
            "status": "online",
            "hostname": hostname,
            "ip_address": ip_address,
            "config_loaded": True,
        }
    except Exception as e:
        log.error(f"Status error: {e}")
        return JSONResponse(content={"status": "degraded", "error": str(e)}, status_code=200)


# ─────────────────────────────────────────────────────────────────────────────
# Config routes (retained for existing consumers; detection-mode field kept
# in model for backward compat but no longer surfaced in the UI)
# ─────────────────────────────────────────────────────────────────────────────

@app.get("/api/config", response_model=ConfigModel)
def get_config():
    try:
        return read_config()
    except Exception as e:
        log.error(f"Config read error: {e}")
        return JSONResponse(content={"error": "Config unavailable"}, status_code=500)


@app.post("/api/config", response_model=ConfigModel)
def update_config(config: ConfigModel, request: Request):
    try:
        old_config = read_config()
        new_config = config.dict()
        write_config(new_config)

        timestamp      = get_timestamp()
        client_ip      = request.client.host if request.client else "unknown"
        client_mac     = get_mac_from_ip(client_ip) if client_ip else None
        user_agent     = request.headers.get("user-agent", "unknown")
        changed_fields = diff_configs(old_config, new_config)

        log_entry = {
            "timestamp":     timestamp,
            "client_ip":     client_ip,
            "client_mac":    client_mac,
            "user_agent":    user_agent,
            "changed_fields": changed_fields,
            "old_config":    old_config,
            "new_config":    new_config,
        }

        log_config_change(
            timestamp=timestamp,
            client_ip=client_ip,
            client_mac=client_mac,
            user_agent=user_agent,
            changed_fields=json.dumps(changed_fields),
            old_config=json.dumps(old_config),
            new_config=json.dumps(new_config),
        )
        write_file_log(log_entry)
        return config

    except Exception as e:
        log.error(f"Config update error: {e}")
        return JSONResponse(content={"error": str(e)}, status_code=500)


# ─────────────────────────────────────────────────────────────────────────────
# Camera control endpoints  (NEW)
# ─────────────────────────────────────────────────────────────────────────────

@app.get("/api/camera/status")
def camera_status():
    """
    Returns current camera runtime state.

    Response example:
    {
        "camera_enabled": true,
        "detector_running": true,
        "is_recording": false,
        "current_recording_file": null,
        "last_recorded_file": "person20260615184530.mp4"
    }
    """
    return camera_manager.get_status()


@app.post("/api/camera/on")
def camera_on():
    """Turn the camera detector ON. Idempotent."""
    result = camera_manager.turn_on()
    status_code = 200 if result["ok"] else 500
    return JSONResponse(content=result, status_code=status_code)


@app.post("/api/camera/off")
def camera_off():
    """Turn the camera detector OFF cleanly. Idempotent."""
    result = camera_manager.turn_off()
    status_code = 200 if result["ok"] else 500
    return JSONResponse(content=result, status_code=status_code)
