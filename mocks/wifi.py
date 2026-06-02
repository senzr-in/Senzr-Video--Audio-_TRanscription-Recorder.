from pathlib import Path
import json
from datetime import datetime

BASE_DIR = Path(__file__).resolve().parent.parent
WIFI_STATE_FILE = BASE_DIR / "configs" / "wifi_state.json"
APP_CONFIG_FILE = BASE_DIR / "configs" / "app_config.json"
RUNTIME_STATE_FILE = BASE_DIR / "configs" / "runtime_state.json"


def _read_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _write_json(path, data):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def _touch_runtime(status=None):
    runtime = _read_json(RUNTIME_STATE_FILE)
    if status:
        runtime["status"] = status
    runtime["last_updated"] = datetime.utcnow().isoformat()
    _write_json(RUNTIME_STATE_FILE, runtime)


def get_status():
    return _read_json(WIFI_STATE_FILE)


def sync_from_config():
    app_config = _read_json(APP_CONFIG_FILE)
    wifi_state = _read_json(WIFI_STATE_FILE)

    wifi_state["ssid"] = app_config["wifi"]["ssid"]
    wifi_state["channel"] = app_config["wifi"]["channel"]

    _write_json(WIFI_STATE_FILE, wifi_state)
    _touch_runtime("wifi_config_synced")
    return wifi_state


def start_ap():
    app_config = _read_json(APP_CONFIG_FILE)
    wifi_state = _read_json(WIFI_STATE_FILE)

    wifi_state["ap_running"] = True
    wifi_state["client_mode_enabled"] = False
    wifi_state["ssid"] = app_config["wifi"]["ssid"]
    wifi_state["channel"] = app_config["wifi"]["channel"]

    _write_json(WIFI_STATE_FILE, wifi_state)
    _touch_runtime("ap_running")
    return wifi_state


def stop_ap():
    wifi_state = _read_json(WIFI_STATE_FILE)

    wifi_state["ap_running"] = False
    wifi_state["connected_clients"] = []

    _write_json(WIFI_STATE_FILE, wifi_state)
    _touch_runtime("ap_stopped")
    return wifi_state


def enable_client_mode():
    wifi_state = _read_json(WIFI_STATE_FILE)

    wifi_state["client_mode_enabled"] = True
    wifi_state["ap_running"] = False

    _write_json(WIFI_STATE_FILE, wifi_state)
    _touch_runtime("client_mode_enabled")
    return wifi_state


def connect_client(device_name):
    wifi_state = _read_json(WIFI_STATE_FILE)

    if device_name not in wifi_state["connected_clients"]:
        wifi_state["connected_clients"].append(device_name)

    _write_json(WIFI_STATE_FILE, wifi_state)
    _touch_runtime("client_connected")
    return wifi_state


def disconnect_client(device_name):
    wifi_state = _read_json(WIFI_STATE_FILE)

    wifi_state["connected_clients"] = [
        client for client in wifi_state["connected_clients"]
        if client != device_name
    ]

    _write_json(WIFI_STATE_FILE, wifi_state)
    _touch_runtime("client_disconnected")
    return wifi_state


def set_uplink(connected: bool):
    wifi_state = _read_json(WIFI_STATE_FILE)
    wifi_state["uplink_connected"] = bool(connected)

    _write_json(WIFI_STATE_FILE, wifi_state)
    _touch_runtime("uplink_updated")
    return wifi_state