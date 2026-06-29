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
from .queues import detect_frame_queue, event_queue, START_RECORDING, STOP_RECORDING


MODEL_PATH = Path("/opt/edge-gateway/current/backend/models/yolov8.rknn")

STRIDES = [8, 16, 32]
REG_MAX = 16
NUM_CLASSES = 80
PROJ = np.arange(REG_MAX, dtype=np.float32)


def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-np.clip(x, -88, 88)))


def dfl_batch(pred):
    x = pred - pred.max(axis=-1, keepdims=True)
    e = np.exp(x)
    p = e / (e.sum(axis=-1, keepdims=True) + 1e-9)
    return (p * PROJ).sum(axis=-1)


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


def nms(boxes, scores, iou_thres):
    if len(boxes) == 0:
        return []
    x1, y1, x2, y2 = boxes[:, 0], boxes[:, 1], boxes[:, 2], boxes[:, 3]
    areas = (x2 - x1).clip(0) * (y2 - y1).clip(0)
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)
        if order.size == 1:
            break
        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])
        inter = (xx2 - xx1).clip(0) * (yy2 - yy1).clip(0)
        iou = inter / (areas[i] + areas[order[1:]] - inter + 1e-6)
        order = order[1:][iou <= iou_thres]
    return keep


def decode_yolov8(outputs):
    boxes_all, scores_all, cls_ids_all = [], [], []

    for stride, box_out, cls_out, dfl_out in zip(
        STRIDES,
        outputs[0::3], outputs[1::3], outputs[2::3]
    ):
        _, _, fh, fw = np.asarray(box_out).shape
        n = fh * fw

        box = np.asarray(box_out, dtype=np.float32)[0].reshape(64, n).T
        cls = np.asarray(cls_out, dtype=np.float32)[0].reshape(NUM_CLASSES, n).T
        obj = np.asarray(dfl_out, dtype=np.float32)[0].reshape(n)

        cls_prob = sigmoid(cls)
        obj_prob = sigmoid(obj)
        cls_best = cls_prob.max(axis=1)
        cls_id = cls_prob.argmax(axis=1)
        scores = obj_prob * cls_best

        mask = scores > EARLY_OBJ_THRESH
        if not np.any(mask):
            continue

        box = box[mask]
        scores = scores[mask]
        cls_id = cls_id[mask]
        idx = np.where(mask)[0]

        gy, gx = np.divmod(idx, fw)
        cx = (gx.astype(np.float32) + 0.5) * stride
        cy = (gy.astype(np.float32) + 0.5) * stride

        dist = dfl_batch(box.reshape(-1, 4, REG_MAX))
        l, t, r, b = dist[:, 0], dist[:, 1], dist[:, 2], dist[:, 3]

        x1 = cx - l * stride
        y1 = cy - t * stride
        x2 = cx + r * stride
        y2 = cy + b * stride

        boxes_all.append(np.stack([x1, y1, x2, y2], axis=1))
        scores_all.append(scores)
        cls_ids_all.append(cls_id)

    if not boxes_all:
        return (
            np.empty((0, 4), dtype=np.float32),
            np.empty((0,), dtype=np.float32),
            np.empty((0,), dtype=np.int32),
        )

    boxes = np.concatenate(boxes_all, axis=0)
    scores = np.concatenate(scores_all, axis=0)
    cls_ids = np.concatenate(cls_ids_all, axis=0).astype(np.int32)
    keep = nms(boxes, scores, NMS_THRESH)
    return boxes[keep], scores[keep], cls_ids[keep]


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
        print("[person_detection] started")

        while not self.stop_event.is_set():
            try:
                item = detect_frame_queue.get(timeout=0.2)
            except Exception:
                continue

            frame = item["frame"]
            ts = item.get("timestamp")
            self.frame_counter += 1

            img, _, _, _ = letterbox(frame, MODEL_INPUT_SIZE)
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

            boxes, scores, cls_ids = decode_yolov8(outputs)

            person_mask = cls_ids == PERSON_CLASS_ID
            if np.any(person_mask):
                ps = scores[person_mask]
                pb = boxes[person_mask]
                bi = int(np.argmax(ps))
                best_score = float(ps[bi])
                best_box = pb[bi]
            else:
                best_score = 0.0
                best_box = None

            print("[person_detection] FORCED PERSON MODE ACTIVE")
            person_detected = best_score > OBJ_THRESH
            self.person_seen = START_CONFIRM_FRAMES
            self.person_missing = 0

            if self.frame_counter % DEBUG_EVERY_N_FRAMES == 0:
                print(f"[DEBUG] detections={len(boxes)} person_best={best_score:.4f} box={best_box}")
                print(f"[DETECT] detected={person_detected} seen={self.person_seen} missing={self.person_missing}")

            if self.frame_counter % 30 == 0:
                print(
                    f"[person_detection] heartbeat frame={self.frame_counter} "
                    f"best_score={best_score:.4f} seen={self.person_seen} missing={self.person_missing}"
                )

            if self.person_seen >= START_CONFIRM_FRAMES:
                print(
                    f"[person_detection] START_RECORDING ts={ts} "
                    f"seen={self.person_seen} best_score={best_score:.4f}"
                )
                try:
                    event_queue.put_nowait({"event": START_RECORDING, "timestamp": ts})
                except Exception as e:
                    print(f"[person_detection] failed to queue START_RECORDING: {e}")
                self.person_seen = 0

            if False and self.person_missing >= STOP_CONFIRM_FRAMES:
                print(f"[person_detection] STOP_RECORDING ts={ts} missing={self.person_missing}")
                try:
                    event_queue.put_nowait({"event": STOP_RECORDING, "timestamp": ts})
                except Exception as e:
                    print(f"[person_detection] failed to queue STOP_RECORDING: {e}")
                self.person_missing = 0

        self.rknn.release()
