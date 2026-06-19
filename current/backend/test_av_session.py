#!/usr/bin/env python3
import sys
import os
import threading
import time

# Make /opt/edge-gateway importable as "current"
sys.path.insert(0, "/opt/edge-gateway")

from current.backend.session_pipeline.queues import stop_event, video_frame_queue, audio_frame_queue
from current.backend.session_pipeline.video_capture import video_capture_loop
from current.backend.session_pipeline.audio_capture import audio_capture_loop
from current.backend.session_pipeline.recorder_manager import RecorderManager
from current.backend.session_pipeline.models import RecordingEvent


def main():
    print("[TEST-AV] Starting AV session test")

    # Start capture threads
    t_video = threading.Thread(target=video_capture_loop, daemon=True)
    t_audio = threading.Thread(target=audio_capture_loop, daemon=True)
    t_video.start()
    t_audio.start()

    mgr = RecorderManager()

    # Manually synthesize START event
    now_ts = time.time()
    start_event = RecordingEvent(type="START_RECORDING", timestamp=now_ts)
    mgr.handle_event(start_event)

    RECORD_SECONDS = 10
    end_time = time.time() + RECORD_SECONDS

    try:
        while time.time() < end_time:
            # Pump frames/chunks into manager (similar to recorder_manager_loop)
            try:
                vf = video_frame_queue.get(timeout=0.05)
                mgr.handle_video_frame(vf.frame)
            except Exception:
                pass

            try:
                ac = audio_frame_queue.get(timeout=0.05)
                mgr.handle_audio_chunk(ac.data)
            except Exception:
                pass
    finally:
        # Manual STOP event
        stop_ev = RecordingEvent(type="STOP_RECORDING", timestamp=time.time())
        mgr.handle_event(stop_ev)
        stop_event.set()
        time.sleep(1.0)

    print("[TEST-AV] Test complete")


if __name__ == "__main__":
    main()
