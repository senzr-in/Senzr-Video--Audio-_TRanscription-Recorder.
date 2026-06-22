import time
import queue
import threading
import numpy as np
import cv2
from rknnlite.api import RKNNLite

from session_pipeline.config import (
    MODEL_PATH, MODEL_INPUT_SIZE,
    PERSON_CLASS_ID, OBJ_THRESH, EARLY_OBJ_THRESH, NMS_THRESH,
    START_THRESHOLD, GRACE_PERIOD_SEC,
    FRAME_W, FRAME_H,
)
from session_pipeline.queues import video_frame_queue


def _sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


def _softmax(x, axis=-1):
    x = x - np.max(x, axis=axis, keepdims=True)
    e = np.exp(x)
    return e / np.sum(e, axis=axis, keepdims=True)


def _letterbox(img, new_shape=640, color=(114, 114, 114)):
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


def _dfl_decode(box_tensor):
    c, h, w = box_tensor.shape
    box_tensor = box_tensor.reshape(4, 16, h, w)
    probs = _softmax(box_tensor, axis=1)
    bins = np.arange(16, dtype=np.float32).reshape(1, 16, 1, 1)
    return np.sum(probs * bins, axis=1)


def _decode_branch(box_map, cls_map, score_map, stride, scale, pad_left, pad_top, orig_w, orig_h):
    boxes, scores, class_ids = [], [], []
    if score_map.ndim == 3:
        score_map = score_map[0]
    cls_map = _sigmoid(cls_map)
    score_map = _sigmoid(score_map)
    dists = _dfl_decode(box_map)
    _, h, w = cls_map.shape

    for gy in range(h):
        for gx in range(w):
            obj_score = float(score_map[gy, gx])
            if obj_score < EARLY_OBJ_THRESH:
                continue
            person_cls = float(cls_map[PERSON_CLASS_ID, gy, gx])
            person_score = max(person_cls, person_cls * obj_score)
            if person_score < OBJ_THRESH:
                continue
            left_d = float(dists[0, gy, gx]) * stride
            top_d  = float(dists[1, gy, gx]) * stride
            right_d = float(dists[2, gy, gx]) * stride
            bot_d  = float(dists[3, gy, gx]) * stride
            cx = (gx + 0.5) * stride
            cy = (gy + 0.5) * stride
            x1 = max(0, min(orig_w - 1, (cx - left_d - pad_left) / scale))
            y1 = max(0, min(orig_h - 1, (cy - top_d  - pad_top)  / scale))
            x2 = max(0, min(orig_w - 1, (cx + right_d - pad_left) / scale))
            y2 = max(0, min(orig_h - 1, (cy + bot_d  - pad_top)  / scale))
            if x2 <= x1 or y2 <= y1:
                continue
            boxes.append([int(x1), int(y1), int(x2 - x1), int(y2 - y1)])
            scores.append(person_score)
            class_ids.append(PERSON_CLASS_ID)
    return boxes, scores, class_ids


def _decode_yolov8(outputs, orig_w, orig_h, scale, pad_left, pad_top):
    if len(outputs) != 9:
        raise RuntimeError(f"Expected 9 outputs, got {len(outputs)}")
    all_boxes, all_scores, all_ids = [], [], []
    for box_raw, cls_raw, score_raw, stride in [
        (outputs[0], outputs[1], outputs[2], 8),
        (outputs[3], outputs[4], outputs[5], 16),
        (outputs[6], outputs[7], outputs[8], 32),
    ]:
        b, s, c = _decode_branch(
            np.squeeze(box_raw, 0), np.squeeze(cls_raw, 0), np.squeeze(score_raw, 0),
            stride, scale, pad_left, pad_top, orig_w, orig_h,
        )
        all_boxes.extend(b); all_scores.extend(s); all_ids.extend(c)
    if not all_boxes:
        return [], [], []
    keep = cv2.dnn.NMSBoxes(all_boxes, all_scores, OBJ_THRESH, NMS_THRESH)
    if keep is None or len(keep) == 0:
        return [], [], []
    keep = np.array(keep).reshape(-1)
    return [all_boxes[i] for i in keep], [all_scores[i] for i in keep], [all_ids[i] for i in keep]


class PersonDetection:
    """
    Reads video_frame_queue.
    Emits START_RECORDING / STOP_RECORDING via event_queue.
    Never touches files, audio, or uploads.
    """

    START_EVENT = "START_RECORDING"
    STOP_EVENT = "STOP_RECORDING"

    def __init__(self, event_queue: queue.Queue):
        self._event_queue = event_queue
        self._rknn = RKNNLite()
        ret = self._rknn.load_rknn(str(MODEL_PATH))
        if ret != 0:
            raise RuntimeError(f"RKNN load failed: {ret}")
        ret = self._rknn.init_runtime(core_mask=RKNNLite.NPU_CORE_AUTO)
        if ret != 0:
            raise RuntimeError(f"RKNN init failed: {ret}")

        self._recording = False
        self._seen_streak = 0
        self._last_seen_time = None
        self._frame_count = 0

    def run(self, stop_event: threading.Event):
        print("[PersonDetection] Starting")
        try:
            while not stop_event.is_set():
                try:
                    item = video_frame_queue.get(timeout=1.0)
                except queue.Empty:
                    continue

                frame = item["frame"]
                orig_h, orig_w = frame.shape[:2]
                inp, scale, pad_left, pad_top = _letterbox(frame, MODEL_INPUT_SIZE)
                inp = cv2.cvtColor(inp, cv2.COLOR_BGR2RGB)
                inp = np.expand_dims(inp, axis=0)

                try:
                    outputs = self._rknn.inference(inputs=[inp])
                    boxes, scores, ids = _decode_yolov8(outputs, orig_w, orig_h, scale, pad_left, pad_top)
                except Exception as e:
                    print(f"[PersonDetection] inference error: {e}")
                    continue

                person_found = any(cid == PERSON_CLASS_ID for cid in ids)
                now = time.monotonic()
                self._frame_count += 1

                if person_found:
                    self._seen_streak += 1
                    self._last_seen_time = now
                else:
                    self._seen_streak = 0

                if not self._recording:
                    if self._seen_streak >= START_THRESHOLD:
                        self._recording = True
                        self._event_queue.put(self.START_EVENT)
                        print("[PersonDetection] -> START_RECORDING")
                else:
                    absent_sec = (now - self._last_seen_time) if self._last_seen_time else GRACE_PERIOD_SEC + 1
                    if absent_sec >= GRACE_PERIOD_SEC:
                        self._recording = False
                        self._seen_streak = 0
                        self._event_queue.put(self.STOP_EVENT)
                        print("[PersonDetection] -> STOP_RECORDING")

                if self._frame_count % 30 == 0:
                    best = max((s for c, s in zip(ids, scores) if c == PERSON_CLASS_ID), default=0.0)
                    print(f"[PersonDetection] frame={self._frame_count} person={person_found} best={best:.2f} streak={self._seen_streak} recording={self._recording}")

        finally:
            self._rknn.release()
            print("[PersonDetection] RKNN released")
