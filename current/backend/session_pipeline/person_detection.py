import cv2
import numpy as np
import threading
from pathlib import Path
from rknnlite.api import RKNNLite

from .config import (
    MODEL_INPUT_SIZE, PERSON_CLASS_ID,
    OBJ_THRESH, EARLY_OBJ_THRESH, NMS_THRESH,
    START_CONFIRM_FRAMES, STOP_CONFIRM_FRAMES, DEBUG_EVERY_N_FRAMES,
)
from .queues import video_frame_queue, event_queue


MODEL_PATH = Path("/opt/edge-gateway/current/backend/models/yolov8.rknn")


def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


def softmax(x, axis=-1):
    x = x - np.max(x, axis=axis, keepdims=True)
    e = np.exp(x)
    return e / np.sum(e, axis=axis, keepdims=True)


def letterbox(img, new_shape=640, color=(114, 114, 114)):
    h, w = img.shape[:2]
    scale = min(new_shape / h, new_shape / w)
    nw, nh = int(round(w * scale)), int(round(h * scale))
    resized = cv2.resize(img, (nw, nh), interpolation=cv2.INTER_LINEAR)
    pad_left = (new_shape - nw) // 2
    pad_top = (new_shape - nh) // 2
    out = np.full((new_shape, new_shape, 3), color, dtype=np.uint8)
    out[pad_top:pad_top + nh, pad_left:pad_left + nw] = resized
    return out, scale, pad_left, pad_top


class PersonDetectionWorker:
    def __init__(self, stop_event: threading.Event):
        self.stop_event = stop_event
        if not MODEL_PATH.exists():
            raise RuntimeError(f"Missing RKNN model: {MODEL_PATH}")
        self.rknn = RKNNLite()
        self.person_seen = 0
        self.person_missing = 0
        self.frame_counter = 0
        ret = self.rknn.load_rknn(str(MODEL_PATH))
        if ret != 0:
            raise RuntimeError(f"Failed to load RKNN model: {ret}")
        ret = self.rknn.init_runtime(core_mask=RKNNLite.NPU_CORE_AUTO)
        if ret != 0:
            raise RuntimeError(f"Failed to init RKNN runtime: {ret}")

    def run(self):
        while not self.stop_event.is_set():
            try:
                item = video_frame_queue.get(timeout=0.2)
            except Exception:
                continue

            frame = item["frame"]
            self.frame_counter += 1
            img, scale, pad_left, pad_top = letterbox(frame, MODEL_INPUT_SIZE)
            img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
            img = np.expand_dims(img, axis=0)
            outputs = self.rknn.inference(inputs=[img])

            if self.frame_counter % DEBUG_EVERY_N_FRAMES == 0:
                shapes = [o.shape for o in outputs]
                max_scores = []
                for o in outputs:
                    arr = np.asarray(o)
                    max_scores.append(float(np.nanmax(arr)))
                print(f"[DEBUG] raw output shapes: {shapes}")
                print(f"[DEBUG] max scores: {max_scores}")

            best_person_score = 0.0
            person_detected = best_person_score > OBJ_THRESH

            if person_detected:
                self.person_seen += 1
                self.person_missing = 0
            else:
                self.person_missing += 1
                self.person_seen = 0

            if self.frame_counter % DEBUG_EVERY_N_FRAMES == 0:
                print(f"[DETECT] detected={person_detected} seen={self.person_seen} missing={self.person_missing}")

            if self.person_seen >= START_CONFIRM_FRAMES:
                event_queue.put_nowait({"event": "START_RECORDING", "timestamp": item["timestamp"]})
                self.person_seen = 0

            if self.person_missing >= STOP_CONFIRM_FRAMES:
                event_queue.put_nowait({"event": "STOP_RECORDING", "timestamp": item["timestamp"]})
                self.person_missing = 0

        self.rknn.release()
