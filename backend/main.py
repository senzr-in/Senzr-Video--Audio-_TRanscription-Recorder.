from services.wifi_service import apply_wifi_settings, restart_wifi_services
from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse
from pydantic import BaseModel
from pathlib import Path
import json

CONFIG_PATH = Path(__file__).parent.parent / "configs" / "device-config.json"

class DeviceConfig(BaseModel):
    mode: str = "face"      # "face" or "object"
    wifi_ssid: str = ""
    wifi_password: str = ""

app = FastAPI(title="Edge Gateway Framework")

frontend_path = Path(__file__).parent.parent / "frontend" / "public"
app.mount("/static", StaticFiles(directory=frontend_path), name="static")

@app.get("/", response_class=HTMLResponse)
def root():
    index_file = frontend_path / "index.html"
    return index_file.read_text()


def load_config() -> DeviceConfig:
    if CONFIG_PATH.exists():
        data = json.loads(CONFIG_PATH.read_text())
        return DeviceConfig(**data)
    return DeviceConfig()


def save_config(config: DeviceConfig) -> None:
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    CONFIG_PATH.write_text(config.json(indent=2))


@app.get("/health")
def health_check():
    return {"status": "ok"}


@app.get("/config", response_model=DeviceConfig)
def get_config():
    return load_config()


@app.post("/config", response_model=DeviceConfig)
def update_config(new_config: DeviceConfig):
    save_config(new_config)
    ok, msg = apply_wifi_settings()
    if not ok:
        # you might later raise HTTPException(status_code=400, detail=msg)
        print(f"[wifi] apply failed: {msg}")
    else:
        print(f"[wifi] apply ok: {msg}")

    restart_wifi_services()
    return new_config