from flask import Blueprint, jsonify
from backend.services.wifi_service import get_wifi_status
from backend.services.inference_service import get_inference_status
from backend.services.platform_service import get_platform_mode
from backend.services.system_service import nginx_status, flask_process_status
from pathlib import Path
import json

status_bp = Blueprint("status", __name__)

RUNTIME_STATE_FILE = Path(__file__).resolve().parent.parent.parent / "configs" / "runtime_state.json"


@status_bp.route("/status", methods=["GET"])
def status():
    with open(RUNTIME_STATE_FILE, "r", encoding="utf-8") as f:
        runtime = json.load(f)

    return jsonify({
        "runtime": runtime,
        "platform": {
            "mode": get_platform_mode(),
            "nginx": nginx_status(),
            "flask": flask_process_status()
        },
        "wifi": get_wifi_status(),
        "inference": get_inference_status(),
    })