from pathlib import Path
import json
from datetime import datetime
import uuid

BASE_DIR = Path(__file__).resolve().parent.parent
CAMERA_STATE_FILE = BASE_DIR / "configs" / "camera_state.json"
APP_CONFIG_FILE = BASE_DIR / "configs" / "app_config.json"
RUNTIME_STATE_FILE = BASE_DIR / "configs" / "runtime_state.json"


def _read_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _write_json(path, data):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def _touch_runtime(active_mode):
    runtime = _read_json(RUNTIME_STATE_FILE)
    runtime["active_mode"] = active_mode
    runtime["last_updated"] = datetime.utcnow().isoformat()
    _write_json(RUNTIME_STATE_FILE, runtime)


def get_status():
    return _read_json(CAMERA_STATE_FILE)


def sync_from_config():
    app_config = _read_json(APP_CONFIG_FILE)
    camera_state = _read_json(CAMERA_STATE_FILE)

    camera_state["detection_mode"] = app_config["inference"]["mode"]

    _write_json(CAMERA_STATE_FILE, camera_state)
    _touch_runtime(camera_state["detection_mode"])
    return camera_state


def set_mode(mode):
    if mode not in ["face", "object"]:
        raise ValueError("mode must be 'face' or 'object'")

    app_config = _read_json(APP_CONFIG_FILE)
    camera_state = _read_json(CAMERA_STATE_FILE)

    app_config["inference"]["mode"] = mode
    camera_state["detection_mode"] = mode

    _write_json(APP_CONFIG_FILE, app_config)
    _write_json(CAMERA_STATE_FILE, camera_state)
    _touch_runtime(mode)
    return camera_state


def start_inference():
    camera_state = _read_json(CAMERA_STATE_FILE)
    camera_state["inference_running"] = True
    _write_json(CAMERA_STATE_FILE, camera_state)
    return camera_state


def stop_inference():
    camera_state = _read_json(CAMERA_STATE_FILE)
    camera_state["inference_running"] = False
    _write_json(CAMERA_STATE_FILE, camera_state)
    return camera_state


def capture_frame():
    camera_state = _read_json(CAMERA_STATE_FILE)

    frame_id = f"frame-{uuid.uuid4().hex[:8]}"
    camera_state["last_frame_id"] = frame_id

    _write_json(CAMERA_STATE_FILE, camera_state)
    return {"frame_id": frame_id}


def run_inference():
    camera_state = _read_json(CAMERA_STATE_FILE)
    mode = camera_state["detection_mode"]

    if mode == "face":
        results = [
            {"label": "face", "confidence": 0.98, "box": [110, 72, 60, 60]}
        ]
    else:
        results = [
            {"label": "person", "confidence": 0.95, "box": [90, 40, 88, 170]},
            {"label": "bottle", "confidence": 0.87, "box": [240, 130, 28, 75]}
        ]

    camera_state["last_inference"] = results
    camera_state["last_inference_time"] = datetime.utcnow().isoformat()

    _write_json(CAMERA_STATE_FILE, camera_state)
    _touch_runtime(mode)
    return results