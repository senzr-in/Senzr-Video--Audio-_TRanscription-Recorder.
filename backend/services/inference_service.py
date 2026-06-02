import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from mocks.camera import (
    get_status,
    set_mode,
    start_inference,
    stop_inference,
    capture_frame,
    run_inference,
    sync_from_config,
)


def get_inference_status():
    return get_status()


def change_mode(mode: str):
    result = set_mode(mode)
    return result


def start():
    sync_from_config()        # Align running mode with app_config.json
    return start_inference()


def stop():
    return stop_inference()


def run_once():
    frame = capture_frame()
    detections = run_inference()
    return {
        "frame_id": frame["frame_id"],
        "detections": detections
    }