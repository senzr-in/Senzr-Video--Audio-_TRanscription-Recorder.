import threading
import time
from pathlib import Path

import cv2

from .config import CAMERA_INDEX, FRAME_W, FRAME_H, FRAME_RATE


class VideoCaptureWorker:
    def __init__(self, out_path: Path, stop_event: threading.Event):
        self.out_path = Path(out_path)
        self.stop_event = stop_event
        self.cap = None
        self.writer = None

    def run(self):
        self.cap = cv2.VideoCapture(CAMERA_INDEX)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_W)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_H)

        if not self.cap.isOpened():
            raise RuntimeError(f"Cannot open camera index {CAMERA_INDEX}")

        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        self.writer = cv2.VideoWriter(str(self.out_path), fourcc, FRAME_RATE, (FRAME_W, FRAME_H))

        while not self.stop_event.is_set():
            ret, frame = self.cap.read()
            if not ret:
                time.sleep(0.05)
                continue
            self.writer.write(frame)

        if self.writer is not None:
            self.writer.release()
        if self.cap is not None:
            self.cap.release()
