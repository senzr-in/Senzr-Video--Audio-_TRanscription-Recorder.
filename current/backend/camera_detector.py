import cv2
import numpy as np
import time
import os
import threading
from datetime import datetime
from pathlib import Path
from rknnlite.api import RKNNLite

# ── Config ────────────────────────────────────────────────────────────────────
MODEL_PATH   = Path(__file__).parent / "models" / "yolov8n.rknn"
RECORDINGS_DIR = Path("/opt/edge-gateway/recordings")
RECORDINGS_DIR.mkdir(parents=True, exist_ok=True)

CAMERA_INDEX  = 0
FRAME_W, FRAME_H = 640, 480
CONFIDENCE_THRESH = 0.50
PERSON_CLASS_ID   = 0          # COCO class 0 = person
GRACE_PERIOD_SEC  = 3.0        # seconds to keep recording after person leaves
INPUT_SIZE        = 640        # YOLOv8n input size

# COCO class names (80 classes)
COCO_NAMES = ["person","bicycle","car","motorcycle","airplane","bus","train",
              "truck","boat","traffic light","fire hydrant","stop sign",
              "parking meter","bench","bird","cat","dog","horse","sheep","cow",
              "elephant","bear","zebra","giraffe","backpack","umbrella","handbag",
              "tie","suitcase","frisbee","skis","snowboard","sports ball","kite",
              "baseball bat","baseball glove","skateboard","surfboard","tennis racket",
              "bottle","wine glass","cup","fork","knife","spoon","bowl","banana",
              "apple","sandwich","orange","broccoli","carrot","hot dog","pizza",
              "donut","cake","chair","couch","potted plant","bed","dining table",
              "toilet","tv","laptop","mouse","remote","keyboard","cell phone",
              "microwave","oven","toaster","sink","refrigerator","book","clock",
              "vase","scissors","teddy bear","hair drier","toothbrush"]


def letterbox(img, target_size=640):
    """Resize with padding to maintain aspect ratio."""
    h, w = img.shape[:2]
    scale = min(target_size / h, target_size / w)
    new_w, new_h = int(w * scale), int(h * scale)
    resized = cv2.resize(img, (new_w, new_h))
    pad_top  = (target_size - new_h) // 2
    pad_left = (target_size - new_w) // 2
    canvas = np.full((target_size, target_size, 3), 114, dtype=np.uint8)
    canvas[pad_top:pad_top+new_h, pad_left:pad_left+new_w] = resized
    return canvas, scale, pad_left, pad_top


def postprocess(outputs, orig_h, orig_w, conf_thresh, scale, pad_left, pad_top):
    """YOLOv8 output: [1, 84, 8400] — decode to boxes."""
    pred = outputs[0][0]          # shape (84, 8400)
    pred = pred.T                 # (8400, 84)
    boxes, scores, class_ids = [], [], []

    for row in pred:
        cx, cy, bw, bh = row[:4]
        class_probs = row[4:]
        class_id = int(np.argmax(class_probs))
        conf = float(class_probs[class_id])
        if conf < conf_thresh:
            continue
        x1 = int((cx - bw / 2 - pad_left) / scale)
        y1 = int((cy - bh / 2 - pad_top)  / scale)
        x2 = int((cx + bw / 2 - pad_left) / scale)
        y2 = int((cy + bh / 2 - pad_top)  / scale)
        x1 = max(0, min(x1, orig_w))
        y1 = max(0, min(y1, orig_h))
        x2 = max(0, min(x2, orig_w))
        y2 = max(0, min(y2, orig_h))
        boxes.append([x1, y1, x2 - x1, y2 - y1])
        scores.append(conf)
        class_ids.append(class_id)

    if not boxes:
        return [], [], []

    indices = cv2.dnn.NMSBoxes(boxes, scores, conf_thresh, 0.45)
    if len(indices) == 0:
        return [], [], []
    indices = indices.flatten()
    return [boxes[i] for i in indices], [scores[i] for i in indices], [class_ids[i] for i in indices]


class PersonDetector:
    def __init__(self, s3_uploader=None):
        self.rknn = RKNNLite()
        ret = self.rknn.load_rknn(str(MODEL_PATH))
        if ret != 0:
            raise RuntimeError(f"Failed to load RKNN model: {ret}")
        ret = self.rknn.init_runtime(core_mask=RKNNLite.NPU_CORE_AUTO)
        if ret != 0:
            raise RuntimeError(f"Failed to init RKNN runtime: {ret}")

        self.cap = cv2.VideoCapture(CAMERA_INDEX)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH,  FRAME_W)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_H)
        if not self.cap.isOpened():
            raise RuntimeError("Cannot open camera /dev/video0")

        fourcc = cv2.VideoWriter_fourcc(*'mp4v')
        self.writer     = None
        self.recording  = False
        self.last_seen  = 0.0
        self.out_path   = None
        self.s3_uploader = s3_uploader
        self._lock = threading.Lock()

    def _start_recording(self):
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.out_path = RECORDINGS_DIR / f"person_{ts}.mp4"
        fourcc = cv2.VideoWriter_fourcc(*'mp4v')
        self.writer = cv2.VideoWriter(str(self.out_path), fourcc, 20.0, (FRAME_W, FRAME_H))
        self.recording = True
        print(f"[CAMERA] Recording started → {self.out_path}")

    def _stop_recording(self):
        if self.writer:
            self.writer.release()
            self.writer = None
        path = self.out_path
        self.recording = False
        self.out_path  = None
        print(f"[CAMERA] Recording saved → {path}")
        if self.s3_uploader and path:
            threading.Thread(target=self.s3_uploader, args=(path,), daemon=True).start()

    def run(self, stop_event: threading.Event):
        print("[CAMERA] Detection loop started")
        while not stop_event.is_set():
            ret, frame = self.cap.read()
            if not ret:
                time.sleep(0.1)
                continue

            # Preprocess
            input_img, scale, pad_left, pad_top = letterbox(frame, INPUT_SIZE)
            input_img = cv2.cvtColor(input_img, cv2.COLOR_BGR2RGB)
            input_img = np.expand_dims(input_img, axis=0)  # (1,640,640,3)

            # NPU inference
            outputs = self.rknn.inference(inputs=[input_img])

            # Postprocess
            boxes, scores, class_ids = postprocess(
                outputs, FRAME_H, FRAME_W,
                CONFIDENCE_THRESH, scale, pad_left, pad_top
            )

            person_detected = any(c == PERSON_CLASS_ID for c in class_ids)

            with self._lock:
                now = time.time()
                if person_detected:
                    self.last_seen = now
                    if not self.recording:
                        self._start_recording()
                elif self.recording and (now - self.last_seen) > GRACE_PERIOD_SEC:
                    self._stop_recording()

                if self.recording and self.writer:
                    self.writer.write(frame)

        # Cleanup on stop
        with self._lock:
            if self.recording:
                self._stop_recording()
        self.cap.release()
        self.rknn.release()
        print("[CAMERA] Detection loop stopped")
