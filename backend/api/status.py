from flask import Blueprint, jsonify
from backend.services.wifi_service import get_wifi_status
from backend.services.inference_service import get_inference_status
from pathlib import Path
import json

status_bp = Blueprint("status", __name__)

RUNTIME_STATE_FILE = Path(__file__).resolve().parent.parent.parent / "configs" / "runtime_state.json"


@status_bp.route("/status", methods=["GET"])
def status():
    """
    Returns a combined system snapshot:
    - runtime state (last action, active mode)
    - wifi state
    - camera/inference state
    """
    with open(RUNTIME_STATE_FILE, "r") as f:
        runtime = json.load(f)

    return jsonify({
        "runtime": runtime,
        "wifi": get_wifi_status(),
        "inference": get_inference_status(),
    })