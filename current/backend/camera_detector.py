import threading
import time
from datetime import datetime
from pathlib import Path

import cv2
import numpy as np
from rknnlite.api import RKNNLite


MODEL_PATH = Path(__file__).parent / "models" / "yolov8n.rknn"
RECORDINGS_DIR = Path("/opt/edge-gateway/recordings")
RECORDINGS_DIR.mkdir(parents=True, exist_ok=True)

CAMERA_INDEX = 0
FRAME_W = 640
FRAME_H = 480
MODEL_INPUT_SIZE = 640

PERSON_CLASS_ID = 0
OBJ_THRESH = 0.25
NMS_THRESH = 0.45
GRACE_PERIOD_SEC = 3.0

DEBUG_EVERY_N_FRAMES = 10

COCO_NAMES = [
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train",
    "truck", "boat", "traffic light", "fire hydrant", "stop sign",
    "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag",
    "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite",
    "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
    "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana",
    "apple", "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza",
    "donut", "cake", "chair", "couch", "potted plant", "bed", "dining table",
    "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
    "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock",
    "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
]


def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


def softmax(x, axis=-1):
    x = x - np.max(x, axis=axis, keepdims=True)
    e = np.exp(x)
    return e / np.sum(e, axis=axis, keepdims=True)


def letterbox(img, new_shape=640, color=(114, 114, 114)):
    h, w = img.shape[:2]
    scale = min(new_shape / h, new_shape / w)

    new_w = int(round(w * scale))
    new_h = int(round(h * scale))

    resized = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_LINEAR)

    pad_left = (new_shape - new_w) // 2
    pad_top = (new_shape - new_h) // 2

    out = np.full((new_shape, new_shape, 3), color, dtype=np.uint8)
    out[pad_top:pad_top + new_h, pad_left:pad_left + new_w] = resized

    return out, scale, pad_left, pad_top


def dfl_decode(box_tensor):
    """
    box_tensor shape: (64, H, W)
    YOLOv8 DFL layout: 4 sides * 16 bins
    returns distances shape: (4, H, W)
    """
    c, h, w = box_tensor.shape
    box_tensor = box_tensor.reshape(4, 16, h, w)
    probs = softmax(box_tensor, axis=1)
    bins = np.arange(16, dtype=np.float32).reshape(1, 16, 1, 1)
    dist = np.sum(probs * bins, axis=1)
    return dist


def decode_branch(box_map, cls_map, score_map, stride, scale, pad_left, pad_top, orig_w, orig_h):
    """
    box_map  : (64, H, W)
    cls_map  : (80, H, W)
    score_map: (1, H, W) or (H, W)
    """
    boxes = []
    scores = []
    class_ids = []

    if score_map.ndim == 3:
        score_map = score_map[0]

    cls_map = sigmoid(cls_map)
    score_map = sigmoid(score_map)
    dists = dfl_decode(box_map)

    _, h, w = cls_map.shape

    for gy in range(h):
        for gx in range(w):
            obj_score = float(score_map[gy, gx])
            if obj_score < 0.05:
                continue

            cls_scores = cls_map[:, gy, gx] * obj_score
            class_id = int(np.argmax(cls_scores))
            score = float(cls_scores[class_id])

            if score < OBJ_THRESH:
                continue

            left_d = float(dists[0, gy, gx]) * stride
            top_d = float(dists[1, gy, gx]) * stride
            right_d = float(dists[2, gy, gx]) * stride
            bottom_d = float(dists[3, gy, gx]) * stride

            cx = (gx + 0.5) * stride
            cy = (gy + 0.5) * stride

            x1 = (cx - left_d - pad_left) / scale
            y1 = (cy - top_d - pad_top) / scale
            x2 = (cx + right_d - pad_left) / scale
            y2 = (cy + bottom_d - pad_top) / scale

            x1 = max(0, min(orig_w - 1, x1))
            y1 = max(0, min(orig_h - 1, y1))
            x2 = max(0, min(orig_w - 1, x2))
            y2 = max(0, min(orig_h - 1, y2))

            if x2 <= x1 or y2 <= y1:
                continue

            boxes.append([int(x1), int(y1), int(x2 - x1), int(y2 - y1)])
            scores.append(score)
            class_ids.append(class_id)

    return boxes, scores, class_ids


def decode_yolov8_rknn(outputs, orig_w, orig_h, scale, pad_left, pad_top):
    """
    Expected RKNN output order from your logs:
      0: (1, 64, 80, 80)
      1: (1, 80, 80, 80)
      2: (1, 1, 80, 80)
      3: (1, 64, 40, 40)
      4: (1, 80, 40, 40)
      5: (1, 1, 40, 40)
      6: (1, 64, 20, 20)
      7: (1, 80, 20, 20)
      8: (1, 1, 20, 20)
    """
    if len(outputs) != 9:
        raise RuntimeError(f"Expected 9 RKNN outputs, got {len(outputs)}")

    all_boxes = []
    all_scores = []
    all_class_ids = []

    branches = [
        (outputs[0], outputs[1], outputs[2], 8),
        (outputs[3], outputs[4], outputs[5], 16),
        (outputs[6], outputs[7], outputs[8], 32),
    ]

    for box_map, cls_map, score_map, stride in branches:
        box_map = np.squeeze(box_map, axis=0)
        cls_map = np.squeeze(cls_map, axis=0)
        score_map = np.squeeze(score_map, axis=0)

        boxes, scores, class_ids = decode_branch(
            box_map, cls_map, score_map, stride,
            scale, pad_left, pad_top, orig_w, orig_h
        )

        all_boxes.extend(boxes)
        all_scores.extend(scores)
        all_class_ids.extend(class_ids)

    if not all_boxes:
        return [], [], []

    keep = cv2.dnn.NMSBoxes(all_boxes, all_scores, OBJ_THRESH, NMS_THRESH)
    if keep is None or len(keep) == 0:
        return [], [], []

    keep = np.array(keep).reshape(-1)

    final_boxes = [all_boxes[i] for i in keep]
    final_scores = [all_scores[i] for i in keep]
    final_class_ids = [all_class_ids[i] for i in keep]

    return final_boxes, final_scores, final_class_ids


class PersonDetector:
    def __init__(self, s3_uploader=None):
        self.s3_uploader = s3_uploader
        self.rknn = RKNNLite()

        ret = self.rknn.load_rknn(str(MODEL_PATH))
        if ret != 0:
            raise RuntimeError(f"Failed to load RKNN model: {ret}")

        ret = self.rknn.init_runtime(core_mask=RKNNLite.NPU_CORE_AUTO)
        if ret != 0:
            raise RuntimeError(f"Failed to init RKNN runtime: {ret}")

        self.cap = cv2.VideoCapture(CAMERA_INDEX)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_W)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_H)

        if not self.cap.isOpened():
            raise RuntimeError(f"Cannot open camera index {CAMERA_INDEX}")

        self.writer = None
        self.recording = False
        self.last_seen_time = 0.0
        self.out_path = None
        self.frame_counter = 0
        self._lock = threading.Lock()

    def _start_recording(self):
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.out_path = RECORDINGS_DIR / f"person_{ts}.mp4"
        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        self.writer = cv2.VideoWriter(str(self.out_path), fourcc, 20.0, (FRAME_W, FRAME_H))
        self.recording = True
        print(f"[CAMERA] Recording started -> {self.out_path}")

    def _stop_recording(self):
        saved_path = self.out_path

        if self.writer is not None:
            self.writer.release()
            self.writer = None

        self.recording = False
        self.out_path = None

        print(f"[CAMERA] Recording stopped -> {saved_path}")

        if self.s3_uploader and saved_path:
            threading.Thread(target=self.s3_uploader, args=(saved_path,), daemon=True).start()

    def _debug_outputs_once(self, outputs):
        print(f"[CAMERA] num outputs: {len(outputs)}")
        for i, out in enumerate(outputs):
            print(f"[CAMERA] output {i} shape={out.shape}, dtype={out.dtype}")

    def run(self, stop_event: threading.Event):
        print("[CAMERA] Detection loop started")
        first_output_dump_done = False

        while not stop_event.is_set():
            ret, frame = self.cap.read()
            if not ret:
                time.sleep(0.1)
                continue

            self.frame_counter += 1
            orig_h, orig_w = frame.shape[:2]

            input_img, scale, pad_left, pad_top = letterbox(frame, MODEL_INPUT_SIZE)
            input_img = cv2.cvtColor(input_img, cv2.COLOR_BGR2RGB)
            input_img = np.expand_dims(input_img, axis=0)

            outputs = self.rknn.inference(inputs=[input_img])

            if not first_output_dump_done:
                self._debug_outputs_once(outputs)
                first_output_dump_done = True

            try:
                boxes, scores, class_ids = decode_yolov8_rknn(
                    outputs,
                    orig_w=orig_w,
                    orig_h=orig_h,
                    scale=scale,
                    pad_left=pad_left,
                    pad_top=pad_top,
                )
            except Exception as e:
                print(f"[CAMERA] postprocess error: {e}")
                time.sleep(0.1)
                continue

            person_scores = [
                scores[i] for i, cid in enumerate(class_ids) if cid == PERSON_CLASS_ID
            ]
            person_detected = len(person_scores) > 0
            best_person_score = max(person_scores) if person_scores else None

            if self.frame_counter % DEBUG_EVERY_N_FRAMES == 0:
                if scores:
                    best_idx = int(np.argmax(scores))
                    best_label = COCO_NAMES[class_ids[best_idx]]
                    best_score = scores[best_idx]
                    best_box = boxes[best_idx]
                    print(
                        f"[CAMERA] best={best_label} "
                        f"score={best_score:.3f} box={best_box} "
                        f"person_detected={person_detected} "
                        f"person_score={best_person_score}"
                    )
                else:
                    print("[CAMERA] no detections")

            with self._lock:
                now = time.time()

                if person_detected:
                    self.last_seen_time = now
                    if not self.recording:
                        self._start_recording()

                elif self.recording and (now - self.last_seen_time) > GRACE_PERIOD_SEC:
                    self._stop_recording()

                if self.recording and self.writer is not None:
                    self.writer.write(frame)

        with self._lock:
            if self.recording:
                self._stop_recording()

        self.cap.release()
        self.rknn.release()
        print("[CAMERA] Detection loop stopped")
