import time
import threading
import queue
from datetime import datetime

import cv2

from session_pipeline.config import (
    CAMERA_INDEX, FRAME_W, FRAME_H, CAMERA_FPS,
)
from session_pipeline.queues import video_frame_queue


class VideoCapture:
    def __init__(self):
        self._cap = None

    def _open_camera(self):
        cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_V4L2)
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_W)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_H)
        cap.set(cv2.CAP_PROP_FPS, CAMERA_FPS)
        if not cap.isOpened():
            raise RuntimeError(f"Cannot open camera index {CAMERA_INDEX}")
        return cap

    def run(self, stop_event: threading.Event):
        print("[VideoCapture] Starting")
        self._cap = self._open_camera()

        try:
            while not stop_event.is_set():
                ret, frame = self._cap.read()
                if not ret:
                    time.sleep(0.05)
                    continue

                item = {
                    "timestamp": datetime.utcnow().isoformat(),
                    "frame": frame,
                }

                try:
                    video_frame_queue.put_nowait(item)
                except queue.Full:
                    try:
                        video_frame_queue.get_nowait()
                    except queue.Empty:
                        pass
                    video_frame_queue.put_nowait(item)

        finally:
            self._cap.release()
            print("[VideoCapture] Camera released")
