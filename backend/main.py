from fastapi import FastAPI, Request
from backend.config_manager import read_config, write_config
from backend.database import init_db, log_config_change
from backend.models import ConfigModel
from backend.logger_utils import get_timestamp, diff_configs, write_file_log, get_mac_from_ip
import socket
import json

app = FastAPI(title="Edge Gateway Framework API")


@app.on_event("startup")
def startup_event():
    init_db()


@app.get("/api/status")
def get_status():
    hostname = socket.gethostname()
    ip_address = socket.gethostbyname(hostname)

    return {
        "status": "online",
        "hostname": hostname,
        "ip_address": ip_address,
        "config_loaded": True
    }


@app.get("/api/config", response_model=ConfigModel)
def get_config():
    return read_config()


@app.post("/api/config", response_model=ConfigModel)
def update_config(config: ConfigModel, request: Request):
    old_config = read_config()
    new_config = config.dict()

    write_config(new_config)

    timestamp = get_timestamp()
    client_ip = request.client.host if request.client else None
    client_mac = get_mac_from_ip(client_ip) if client_ip else None
    user_agent = request.headers.get("user-agent", "unknown")
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