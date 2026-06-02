from flask import Blueprint, jsonify, request
from backend.services import wifi_service

wifi_bp = Blueprint("wifi", __name__)


@wifi_bp.route("/", methods=["GET"])
def get_wifi():
    return jsonify(wifi_service.get_wifi_status())


@wifi_bp.route("/start-ap", methods=["POST"])
def start_ap():
    result = wifi_service.start_access_point()
    return jsonify(result)


@wifi_bp.route("/stop-ap", methods=["POST"])
def stop_ap():
    result = wifi_service.stop_access_point()
    return jsonify(result)


@wifi_bp.route("/clients", methods=["GET"])
def clients():
    status = wifi_service.get_wifi_status()
    return jsonify({"clients": status.get("connected_clients", [])})


@wifi_bp.route("/uplink", methods=["POST"])
def uplink():
    """
    Body: { "connected": true/false }
    Sets the simulated uplink (upstream internet) state.
    """
    data = request.get_json()
    if data is None or "connected" not in data:
        return jsonify({"error": "Missing 'connected' field"}), 400
    result = wifi_service.update_uplink(data["connected"])
    return jsonify(result)