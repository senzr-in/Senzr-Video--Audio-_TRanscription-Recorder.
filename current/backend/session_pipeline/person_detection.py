# current/backend/session_pipeline/person_detection.py
import time
import numpy as np
import cv2

from current.backend.camera_detector import (
    RKNNLite,
    MODEL_PATH,
    PERSON_CLASS_ID,
    letterbox,
    decode_yolov8_rknn,
)
from .queues import video_frame_queue, recording_event_queue, stop_event
from .config import START_CONFIRM_FRAMES, STOP_ABSENT_SECONDS, VIDEO_FPS
from .models import RecordingEvent


def person_detection_loop():
    print("[DETECT] Starting person detection thread")

    rknn = RKNNLite()
    ret = rknn.load_rknn(str(MODEL_PATH))
    if ret != 0:
        print(f"[DETECT] Failed to load RKNN model: {ret}")
        return

    ret = rknn.init_runtime(core_mask=RKNNLite.NPU_CORE_AUTO)
    if ret != 0:
        print(f"[DETECT] Failed to init RKNN runtime: {ret}")
        return

    frame_counter = 0
    person_seen_streak = 0
    person_missing_streak = 0
    currently_recording = False

    missing_frames_for_stop = int(STOP_ABSENT_SECONDS * VIDEO_FPS)

    try:
        while not stop_event.is_set():
            try:
                vf = video_frame_queue.get(timeout=0.1)
            except Exception:
                continue

            frame = vf.frame
            frame_counter += 1
            orig_h, orig_w = frame.shape[:2]

            input_img, scale, pad_left, pad_top = letterbox(frame, 640)
            input_img = cv2.cvtColor(input_img, cv2.COLOR_BGR2RGB)
            input_img = np.expand_dims(input_img, axis=0)

            outputs = rknn.inference(inputs=[input_img])

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
                print(f"[DETECT] postprocess error: {e}")
                time.sleep(0.05)
                continue

            # DEBUG: print summary every 15 frames
            if frame_counter % 15 == 0:
                # Keep it small: just show counts and max score for person class
                total = len(class_ids)
                max_score = max(scores) if len(scores) else 0.0
                person_count = sum(1 for cid in class_ids if cid == PERSON_CLASS_ID)
                print(
                    f"[DETECT] frame={frame_counter} total={total} "
                    f"person_count={person_count} max_score={max_score:.2f} "
                    f"class_ids={list(class_ids)[:5]}"
                )

            person_detected = any(cid == PERSON_CLASS_ID for cid in class_ids)
            if person_detected:
                person_seen_streak += 1
                person_missing_streak = 0
            else:
                person_missing_streak += 1
                person_seen_streak = 0

            if not currently_recording and person_seen_streak >= START_CONFIRM_FRAMES:
                ev = RecordingEvent(type="START_RECORDING", timestamp=vf.timestamp)
                try:
                    recording_event_queue.put(ev, timeout=0.1)
                    currently_recording = True
                    print("[DETECT] START_RECORDING emitted")
                except Exception:
                    pass

            if currently_recording and person_missing_streak >= missing_frames_for_stop:
                ev = RecordingEvent(type="STOP_RECORDING", timestamp=vf.timestamp)
                try:
                    recording_event_queue.put(ev, timeout=0.1)
                    currently_recording = False
                    print("[DETECT] STOP_RECORDING emitted")
                except Exception:
                    pass

    finally:
        rknn.release()
        print("[DETECT] Person detection thread stopped, RKNN released")
