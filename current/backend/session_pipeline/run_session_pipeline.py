# current/backend/session_pipeline/run_session_pipeline.py
import threading
import signal
import time

from .queues import stop_event
from .video_capture import video_capture_loop
from .audio_capture import audio_capture_loop
from .person_detection import person_detection_loop
from .recorder_manager import recorder_manager_loop
from .transcriber import transcriber_loop
from .uploader import uploader_loop


def main():
    print("[PIPELINE] Starting session pipeline")

    threads = [
        threading.Thread(target=video_capture_loop, name="video-capture", daemon=True),
        threading.Thread(target=audio_capture_loop, name="audio-capture", daemon=True),
        threading.Thread(target=person_detection_loop, name="person-detect", daemon=True),
        threading.Thread(target=recorder_manager_loop, name="recorder-manager", daemon=True),
        threading.Thread(target=transcriber_loop, name="transcriber", daemon=True),
        threading.Thread(target=uploader_loop, name="uploader", daemon=True),
    ]

    for t in threads:
        t.start()

    def handle_signal(signum, frame):
        print(f"[PIPELINE] Signal {signum} received, stopping...")
        stop_event.set()

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    try:
        while not stop_event.is_set():
            time.sleep(0.5)
    finally:
        print("[PIPELINE] Waiting for threads to exit...")
        time.sleep(1.0)
        print("[PIPELINE] Shutdown complete")


if __name__ == "__main__":
    main()
