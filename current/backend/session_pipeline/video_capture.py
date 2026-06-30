import time
import cv2

from .queues import detect_frame_queue, record_frame_queue

CAMERA_INDEX = 0
FRAME_W = 640
FRAME_H = 480
FPS = 28.0


class VideoCaptureWorker:
    def __init__(self, stop_event):
        self.stop_event = stop_event

    def run(self):
        cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_V4L2)
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_W)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_H)
        cap.set(cv2.CAP_PROP_FPS, FPS)

        if not cap.isOpened():
            print("[video_capture] ERROR: cannot open camera")
            return

        print("[video_capture] started")
        while not self.stop_event.is_set():
            ret, frame = cap.read()
            if not ret:
                time.sleep(0.05)
                continue

            item = {"timestamp": time.time(), "frame": frame}

            try:
                detect_frame_queue.put_nowait(item)
            except Exception:
                pass

            try:
                record_frame_queue.put_nowait(item)
            except Exception:
                pass

        cap.release()
        print("[video_capture] stopped")
