import time
import cv2

from .queues import video_frame_queue, stop_event
from .models import VideoFrame, now_ts

CAMERA_PATH = "/dev/video0"


def open_camera_with_retry(max_tries: int = 10, delay: float = 1.0):
    for attempt in range(1, max_tries + 1):
        cap = cv2.VideoCapture(CAMERA_PATH, cv2.CAP_V4L2)
        if cap.isOpened():
            print(f"[VIDEO] Opened camera at {CAMERA_PATH} on attempt {attempt}")
            return cap
        print(f"[VIDEO] Attempt {attempt}/{max_tries} failed to open {CAMERA_PATH}, retrying in {delay}s...")
        cap.release()
        time.sleep(delay)
    print(f"[VIDEO] Giving up after {max_tries} attempts to open {CAMERA_PATH}")
    return None


def video_capture_loop():
    cap = open_camera_with_retry()
    if cap is None:
        return

    print(f"[VIDEO] Capture loop started on {CAMERA_PATH}")

    try:
        while not stop_event.is_set():
            ret, frame = cap.read()
            if not ret:
                time.sleep(0.05)
                continue

            ts = now_ts()
            vf = VideoFrame(timestamp=ts, frame=frame)
            try:
                video_frame_queue.put(vf, timeout=0.1)
            except Exception:
                pass
    finally:
        cap.release()
        print("[VIDEO] Capture loop stopped, camera released")
