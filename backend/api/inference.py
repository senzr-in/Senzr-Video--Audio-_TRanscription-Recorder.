from flask import Blueprint, jsonify, request
from backend.services import inference_service

inference_bp = Blueprint("inference", __name__)


@inference_bp.route("/", methods=["GET"])
def get_inference():
    return jsonify(inference_service.get_inference_status())


@inference_bp.route("/mode", methods=["POST"])
def set_mode():
    """
    Body: { "mode": "face" } or { "mode": "object" }
    Changes detection mode on the edge device.
    """
    data = request.get_json()
    if data is None or "mode" not in data:
        return jsonify({"error": "Missing 'mode' field"}), 400

    try:
        result = inference_service.change_mode(data["mode"])
        return jsonify(result)
    except ValueError as e:
        return jsonify({"error": str(e)}), 400


@inference_bp.route("/run", methods=["POST"])
def run():
    """
    Triggers a single local inference cycle.
    Captures a mock frame and runs detection on it.
    """
    result = inference_service.run_once()
    return jsonify(result)


@inference_bp.route("/start", methods=["POST"])
def start():
    result = inference_service.start()
    return jsonify(result)


@inference_bp.route("/stop", methods=["POST"])
def stop():
    result = inference_service.stop()
    return jsonify(result)