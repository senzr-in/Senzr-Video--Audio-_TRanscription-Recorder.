from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pathlib import Path
import socket
import json
import logging

from backend.config_manager import read_config, write_config
from backend.database import init_db, log_config_change
from backend.models import ConfigModel
from backend.logger_utils import get_timestamp, diff_configs, write_file_log, get_mac_from_ip

log = logging.getLogger("edge-gateway")
logging.basicConfig(level=logging.INFO)

FRONTEND_DIR = Path(__file__).parent.parent / "frontend"
INDEX_FILE   = FRONTEND_DIR / "index.html"

app = FastAPI(title="Edge Gateway Framework API")

# Serve static files only if frontend folder exists
if FRONTEND_DIR.exists():
    try:
        app.mount("/static", StaticFiles(directory=str(FRONTEND_DIR)), name="static")
    except Exception as e:
        log.error(f"Could not mount static files: {e}")


@app.on_event("startup")
def startup_event():
    init_db()
    log.info("Edge Gateway started. Backend ready.")


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
            "config_loaded": True
        }
    except Exception as e:
        log.error(f"Status error: {e}")
        return JSONResponse(content={"status": "degraded", "error": str(e)}, status_code=200)


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

        timestamp   = get_timestamp()
        client_ip   = request.client.host if request.client else "unknown"
        client_mac  = get_mac_from_ip(client_ip) if client_ip else None
        user_agent  = request.headers.get("user-agent", "unknown")
        changed_fields = diff_configs(old_config, new_config)

        log_entry = {
            "timestamp": timestamp,
            "client_ip": client_ip,
            "client_mac": client_mac,
            "user_agent": user_agent,
            "changed_fields": changed_fields,
            "old_config": old_config,
            "new_config": new_config
        }

        log_config_change(
            timestamp=timestamp,
            client_ip=client_ip,
            client_mac=client_mac,
            user_agent=user_agent,
            changed_fields=json.dumps(changed_fields),
            old_config=json.dumps(old_config),
            new_config=json.dumps(new_config)
        )

        write_file_log(log_entry)
        return config

    except Exception as e:
        log.error(f"Config update error: {e}")
        return JSONResponse(content={"error": str(e)}, status_code=500)
