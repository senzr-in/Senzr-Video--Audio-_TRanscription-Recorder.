from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pathlib import Path
import socket
import logging
import threading
import json

from backend.configmanager import readconfig, writeconfig
from backend.database import initdb, logconfigchange
from backend.models import ConfigModel
from backend.loggerutils import gettimestamp, diffconfigs, writefilelog, getmacfromip

# Optional: import S3 uploader if AWS is configured
try:
    from backend.s3uploader import uploadrecording as s3_upload_fn
except Exception:
    s3_upload_fn = None

# Optional: import PersonDetector (fails gracefully on non-Pi hardware)
try:
    from backend.camera_detector import PersonDetector
    _DETECTOR_AVAILABLE = True
except Exception:
    PersonDetector = None
    _DETECTOR_AVAILABLE = False

log = logging.getLogger("edge-gateway")
logging.basicConfig(level=logging.INFO)

FRONTEND_DIR = Path(__file__).parent.parent / "frontend"
INDEX_FILE = FRONTEND_DIR / "index.html"

app = FastAPI(title="Edge Gateway Framework API")

if FRONTEND_DIR.exists():
    try:
        app.mount("/static", StaticFiles(directory=str(FRONTEND_DIR)), name="static")
    except Exception as e:
        log.error(f"Could not mount static files: {e}")


class CameraManager:
    """
    Manages the PersonDetector thread lifecycle.
    """

    def __init__(self):
        self._lock = threading.Lock()
        self._detector = None
        self._thread = None
        self._stop_event = None
        self._camera_enabled = False
        self._last_recorded_file = None

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
            return bool(getattr(self._detector, "recording", False))

    @property
    def current_recording_file(self):
        with self._lock:
            if self._detector is None:
                return None
            outpath = getattr(self._detector, "outpath", None)
            return outpath.name if outpath else None

    @property
    def last_recorded_file(self):
        with self._lock:
            if self._detector is not None:
                outpath = getattr(self._detector, "outpath", None)
                if outpath:
                    return outpath.name
            return self._last_recorded_file

    def turn_on(self) -> dict:
        with self._lock:
            if self._camera_enabled and self._thread is not None and self._thread.is_alive():
                return {"ok": True, "message": "Camera already ON"}

            if not _DETECTOR_AVAILABLE:
                return {"ok": False, "message": "Camera detector not available on this hardware"}

            if self._thread is not None and not self._thread.is_alive():
                self._thread = None
                self._detector = None
                self._stop_event = None

            try:
                self._stop_event = threading.Event()
                self._detector = PersonDetector(s3uploader=s3_upload_fn)
                self._thread = threading.Thread(
                    target=self._detector.run,
                    args=(self._stop_event,),
                    daemon=True,
                    name="camera-detector",
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
        with self._lock:
            if not self._camera_enabled and self._thread is None:
                return {"ok": True, "message": "Camera already OFF"}

            self._camera_enabled = False

            detector_ref = self._detector
            if detector_ref is not None:
                outpath = getattr(detector_ref, "outpath", None)
                if outpath:
                    self._last_recorded_file = outpath.name

            if self._stop_event is not None:
                self._stop_event.set()

            thread_ref = self._thread

        if thread_ref is not None:
            thread_ref.join(timeout=10)

        with self._lock:
            if self._detector is not None:
                outpath = getattr(self._detector, "outpath", None)
                if outpath:
                    self._last_recorded_file = outpath.name

            self._thread = None
            self._stop_event = None
            self._detector = None

        log.info("[CameraManager] Camera turned OFF, detector thread stopped")
        return {"ok": True, "message": "Camera turned OFF"}

    def get_status(self) -> dict:
        return {
            "camera_enabled": self.camera_enabled,
            "detector_running": self.detector_running,
            "is_recording": self.is_recording,
            "current_recording_file": self.current_recording_file,
            "last_recorded_file": self.last_recorded_file,
        }


camera_manager = CameraManager()


@app.on_event("startup")
def startup_event():
    initdb()
    log.info("Edge Gateway started. Backend ready. Camera is OFF by default.")


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
            status_code=200,
        )
    except Exception as e:
        log.error(f"Error serving index.html: {e}")
        return HTMLResponse(
            content="<h2>Gateway is running — UI temporarily unavailable.</h2>",
            status_code=200,
        )


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


@app.get("/api/config", response_model=ConfigModel)
def get_config():
    try:
        return readconfig()
    except Exception as e:
        log.error(f"Config read error: {e}")
        return JSONResponse(content={"error": "Config unavailable"}, status_code=500)


@app.post("/api/config", response_model=ConfigModel)
def update_config(config: ConfigModel, request: Request):
    try:
        old_config = readconfig()
        new_config = config.dict()
        writeconfig(new_config)

        timestamp = gettimestamp()
        client_ip = request.client.host if request.client else "unknown"
        client_mac = getmacfromip(client_ip) if client_ip else None
        user_agent = request.headers.get("user-agent", "unknown")
        changed_fields = diffconfigs(old_config, new_config)

        log_entry = {
            "timestamp": timestamp,
            "client_ip": client_ip,
            "client_mac": client_mac,
            "user_agent": user_agent,
            "changed_fields": changed_fields,
            "old_config": old_config,
            "new_config": new_config,
        }

        logconfigchange(
            timestamp=timestamp,
            clientip=client_ip,
            clientmac=client_mac,
            useragent=user_agent,
            changedfields=json.dumps(changed_fields),
            oldconfig=json.dumps(old_config),
            newconfig=json.dumps(new_config),
        )
        writefilelog(log_entry)
        return config

    except Exception as e:
        log.error(f"Config update error: {e}")
        return JSONResponse(content={"error": str(e)}, status_code=500)


@app.get("/api/camera/status")
def camera_status():
    return camera_manager.get_status()


@app.post("/api/camera/on")
def camera_on():
    result = camera_manager.turn_on()
    status_code = 200 if result["ok"] else 500
    return JSONResponse(content=result, status_code=status_code)


@app.post("/api/camera/off")
def camera_off():
    result = camera_manager.turn_off()
    status_code = 200 if result["ok"] else 500
    return JSONResponse(content=result, status_code=status_code)
