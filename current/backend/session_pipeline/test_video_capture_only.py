#!/usr/bin/env python3
import sys
import time
import threading

# Make /opt/edge-gateway importable as "current"
sys.path.insert(0, "/opt/edge-gateway")

from current.backend.session_pipeline.video_capture import video_capture_loop
from current.backend.session_pipeline.queues import stop_event, video_frame_queue


def main():
    print("[TEST-VIDEO] Starting video capture only (5 seconds)")

    t = threading.Thread(target=video_capture_loop, daemon=True)
    t.start()

    start = time.time()
    frames = 0

    try:
        while time.time() - start < 5:
            try:
                vf = video_frame_queue.get(timeout=0.2)
                frames += 1
            except Exception:
                pass
    finally:
        stop_event.set()
        time.sleep(1.0)
        print(f"[TEST-VIDEO] Collected {frames} frames in 5 seconds")


if __name__ == "__main__":
    main()
