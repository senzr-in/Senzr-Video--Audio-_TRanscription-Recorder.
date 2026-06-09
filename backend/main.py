from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from backend.config_manager import read_config, write_config
from backend.database import init_db
from backend.models import ConfigModel
import socket

app = FastAPI(title="Edge Gateway Framework API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://127.0.0.1:5500", "http://localhost:5500"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


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
def update_config(config: ConfigModel):
    write_config(config.dict())
    return config