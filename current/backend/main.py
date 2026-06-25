from fastapi import FastAPI, HTTPException
import socket
import traceback
import logging

from .config_manager import read_config, write_config
from .database import init_db
from .models import ConfigModel
from .session_pipeline.run_session_pipeline import SessionPipeline

logging.basicConfig(level=logging.INFO)
log = logging.getLogger("edge-gateway")

app = FastAPI(title="Edge Gateway Framework API")
_pipeline = SessionPipeline()


@app.on_event("startup")
def startup_event():
    init_db()
    log.info("[main] FastAPI started")


@app.get("/api/status")
def get_status():
    try:
        import netifaces
        addrs = netifaces.ifaddresses("wlan0")
        ip = addrs[netifaces.AF_INET][0]["addr"]
    except Exception:
        ip = socket.gethostbyname(socket.gethostname())

    return {
        "status": "online",
        "hostname": socket.gethostname(),
        "ip_address": ip,
        "config_loaded": True,
        "pipeline_running": _pipeline.is_running,
    }


@app.get("/api/config", response_model=ConfigModel)
def get_config():
    return read_config()


@app.post("/api/config", response_model=ConfigModel)
def update_config(config: ConfigModel):
    write_config(config.dict())
    return config


@app.post("/api/camera/on")
def camera_on():
    if _pipeline.is_running:
        return {"ok": True, "message": "Pipeline already running"}
    try:
        _pipeline.start()
        return {"ok": True, "message": "Pipeline started"}
    except Exception as e:
        traceback.print_exc()
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/api/camera/off")
def camera_off():
    if not _pipeline.is_running:
        return {"ok": True, "message": "Pipeline already stopped"}
    _pipeline.stop()
    return {"ok": True, "message": "Pipeline stopped"}


@app.get("/api/camera/status")
def camera_status():
    return {"pipeline_running": _pipeline.is_running}
