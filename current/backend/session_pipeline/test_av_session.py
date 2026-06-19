#!/usr/bin/env python3
"""
Temporary AV integration test:
- Starts video + audio capture
- Manually starts a single session
- Records ~10 seconds
- Writes video.mp4 + audio.wav + session.json in a session folder
No detection, no S3, no Whisper.
"""
import threading
import time
from datetime import datetime

from .queues import stop_event, video_frame_queue, audio_frame_queue
from .video_capture import video_capture_loop
from .audio_capture import audio_capture_loop
from .recorder_manager import RecorderManager
from .models import RecordingEvent, new_session_id


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
            # Pump frames/chunks into manager (like recorder_manager_loop does)
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
