import time
import cv2

from .config import CAMERA_INDEX, FRAME_WIDTH, FRAME_HEIGHT
from .queues import video_frame_queue, stop_event
from .models import VideoFrame, now_ts


def video_capture_loop():
    cap = cv2.VideoCapture(CAMERA_INDEX)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)

    if not cap.isOpened():
        print(f"[VIDEO] Cannot open camera index {CAMERA_INDEX}")
        return

    print("[VIDEO] Capture loop started")

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
                # Queue full; drop frame
                pass
    finally:
        cap.release()
        print("[VIDEO] Capture loop stopped, camera released")