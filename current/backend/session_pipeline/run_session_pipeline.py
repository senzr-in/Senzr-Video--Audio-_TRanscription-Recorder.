import threading
import time

from current.backend.camera_detector import PersonDetector

from .config import SESSION_BASE_DIR
from .recorder_manager import RecorderManager


def main():
    SESSION_BASE_DIR.mkdir(parents=True, exist_ok=True)

    stop_event = threading.Event()
    recorder = RecorderManager()

    detector = PersonDetector()

    if hasattr(detector, "_start_recording") and hasattr(detector, "_stop_recording"):
        original_start = detector._start_recording
        original_stop = detector._stop_recording

        def wrapped_start():
            meta = recorder.start_session()
            original_start()
            return meta

        def wrapped_stop():
            original_stop()
            recorder.finalize_session()

        detector._start_recording = wrapped_start
        detector._stop_recording = wrapped_stop

    try:
        detector.run(stop_event)
    except KeyboardInterrupt:
        stop_event.set()
    finally:
        try:
            if detector.is_recording:
                detector._stop_recording()
        except Exception:
            pass


if __name__ == "__main__":
    main()
