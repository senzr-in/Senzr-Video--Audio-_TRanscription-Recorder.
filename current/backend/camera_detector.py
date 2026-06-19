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
OBJ_THRESH = 0.5
EARLY_OBJ_THRESH = 0.15
NMS_THRESH = 0.45

START_CONFIRM_FRAMES = 5
STOP_CONFIRM_FRAMES = 40
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
    c, h, w = box_tensor.shape
    box_tensor = box_tensor.reshape(4, 16, h, w)
    probs = softmax(box_tensor, axis=1)
    bins = np.arange(16, dtype=np.float32).reshape(1, 16, 1, 1)
    return np.sum(probs * bins, axis=1)


def decode_branch(box_map, cls_map, score_map, stride, scale, pad_left, pad_top, orig_w, orig_h):
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
            
            if obj_score < EARLY_OBJ_THRESH:
                continue
            
            person_cls = float(cls_map[PERSON_CLASS_ID, gy, gx])
            
            person_score = person_cls * obj_score
            
            if person_score < OBJ_THRESH:
                if person_cls > 0.10:
                    print(
                        f"obj={obj_score:.3f} "
                        f"person={person_cls:.3f} "
                        f"final={person_score:.3f}"
                    )
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
            scores.append(person_score)
            class_ids.append(PERSON_CLASS_ID)
    
    return boxes, scores, class_ids


def decode_yolov8_rknn(outputs, orig_w, orig_h, scale, pad_left, pad_top):
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
        boxes, scores, class_ids = decode_branch(box_map, cls_map, score_map, stride, scale, pad_left, pad_top, orig_w, orig_h)
        all_boxes.extend(boxes)
        all_scores.extend(scores)
        all_class_ids.extend(class_ids)
    
    if not all_boxes:
        return [], [], []
    
    keep = cv2.dnn.NMSBoxes(all_boxes, all_scores, OBJ_THRESH, NMS_THRESH)
    if keep is None or len(keep) == 0:
        return [], [], []
    
    keep = np.array(keep).reshape(-1)
    return [all_boxes[i] for i in keep], [all_scores[i] for i in keep], [all_class_ids[i] for i in keep]


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
        
        self.cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_V4L2)
        self.cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_W)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_H)
        
        if not self.cap.isOpened():
            raise RuntimeError(f"Cannot open camera index {CAMERA_INDEX}")
        
        self._lock = threading.Lock()
        self._writer = None
        self._recording = False
        self._out_path = None
        self._current_recording_file = None
        self._last_recorded_file = None
        self._frame_counter = 0
        self._person_seen_streak = 0
        self._person_missing_streak = 0

    @property
    def is_recording(self):
        with self._lock:
            return self._recording

    @property
    def current_recording_file(self):
        with self._lock:
            return self._current_recording_file

    @property
    def last_recorded_file(self):
        with self._lock:
            return self._last_recorded_file

    def _safe_upload(self, path):
        try:
            self.s3_uploader(path)
        except Exception as e:
            print(f"[UPLOAD] {e}")

    def _start_recording(self):
        ts = datetime.now().strftime("%Y%m%d%H%M%S")
        filename = f"person{ts}.mp4"
        self._out_path = RECORDINGS_DIR / filename
        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        self._writer = cv2.VideoWriter(str(self._out_path), fourcc, 20.0, (FRAME_W, FRAME_H))
        
        if not self._writer.isOpened():
            raise RuntimeError(f"Failed to create video writer: {self._out_path}")
        
        self._recording = True
        self._current_recording_file = filename
        print(f"[CAMERA] Recording started -> {self._out_path}")

    def _stop_recording(self):
        saved_path = self._out_path
        if self._writer is not None:
            self._writer.release()
            self._writer = None
        self._recording = False
        self._out_path = None
        if self._current_recording_file:
            self._last_recorded_file = self._current_recording_file
        self._current_recording_file = None
        print(f"[CAMERA] Recording stopped -> {saved_path}")
        if self.s3_uploader and saved_path:
            threading.Thread(target=self._safe_upload, args=(saved_path,), daemon=True).start()

    def run(self, stop_event: threading.Event):
        print("[CAMERA] Detection loop started")
        first_output_dump_done = False
        
        while not stop_event.is_set():
            ret, frame = self.cap.read()
            if not ret:
                time.sleep(0.1)
                continue
            
            self._frame_counter += 1
            orig_h, orig_w = frame.shape[:2]
            
            input_img, scale, pad_left, pad_top = letterbox(frame, MODEL_INPUT_SIZE)
            input_img = cv2.cvtColor(input_img, cv2.COLOR_BGR2RGB)
            input_img = np.expand_dims(input_img, axis=0)
            
            outputs = self.rknn.inference(inputs=[input_img])
            
            if not first_output_dump_done:
                print(f"[CAMERA] num outputs: {len(outputs)}")
                for i, out in enumerate(outputs):
                    print(f"[CAMERA] output {i} shape={out.shape}, dtype={out.dtype}")
                first_output_dump_done = True
            
            try:
                boxes, scores, class_ids = decode_yolov8_rknn(outputs, orig_w=orig_w, orig_h=orig_h, scale=scale, pad_left=pad_left, pad_top=pad_top)
            except Exception as e:
                print(f"[CAMERA] postprocess error: {e}")
                time.sleep(0.1)
                continue

            best_person_score = 0.0

            for cid, score in zip(class_ids, scores):
                if cid == PERSON_CLASS_ID:
                    best_person_score = max(best_person_score, float(score))

            person_detected = best_person_score > 0.50

            if person_detected:
                self._person_seen_streak += 1
                self._person_missing_streak = 0
            else:
                self._person_missing_streak += 1
                self._person_seen_streak = 0
            
            if self._frame_counter % DEBUG_EVERY_N_FRAMES == 0:
                if person_detected:
                    print(
                        f"[CAMERA] best_person_score={best_person_score:.3f} "
                        f"person_detected={person_detected} "
                        f"recording={self._recording} "
                        f"seen_streak={self._person_seen_streak} "
                        f"missing_streak={self._person_missing_streak}"
                    )
                else:
                    print(
                        f"[CAMERA] no detections "
                        f"seen_streak={self._person_seen_streak} "
                        f"missing_streak={self._person_missing_streak}"
                    )

            with self._lock:
                if not self._recording:
                    if self._person_seen_streak >= START_CONFIRM_FRAMES:
                        self._start_recording()
                else:
                    if self._person_missing_streak >= STOP_CONFIRM_FRAMES:
                        self._stop_recording()
                
                if self._recording and self._writer is not None:
                    self._writer.write(frame)
        
        with self._lock:
            if self._recording:
                self._stop_recording()
        
        self.cap.release()
        self.rknn.release()
        print("[CAMERA] Detection loop stopped, resources released")
