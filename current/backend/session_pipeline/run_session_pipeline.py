import threading
from .queues import video_frame_queue
from .video_capture    import VideoCaptureWorker
from .audio_capture    import AudioCaptureWorker
from .person_detection import PersonDetectionWorker
from .transcriber      import TranscriberWorker
from .recorder_manager import RecorderManagerWorker
from .uploader         import UploaderWorker


class SessionPipeline:
    def __init__(self):
        self._stop_event    = threading.Event()
        self._threads       = []
        self._recording_flag = threading.Event()
        self.is_running     = False

    def start(self):
        if self.is_running:
            return
        self._stop_event.clear()

        transcriber = TranscriberWorker(self._stop_event, self._recording_flag)
        recorder    = RecorderManagerWorker(self._stop_event, video_frame_queue, transcriber)
        transcriber.recording_flag = recorder.recording_flag  # share the same flag

        workers = [
            VideoCaptureWorker(self._stop_event),
            AudioCaptureWorker(self._stop_event),
            PersonDetectionWorker(self._stop_event),
            recorder,
            transcriber,
            UploaderWorker(self._stop_event),
        ]

        for w in workers:
            t = threading.Thread(target=w.run, daemon=True)
            t.start()
            self._threads.append(t)

        self.is_running = True
        print("[pipeline] all 6 workers started")

    def stop(self):
        if not self.is_running:
            return
        self._stop_event.set()
        for t in self._threads:
            t.join(timeout=5)
        self._threads.clear()
        self.is_running = False
        print("[pipeline] all workers stopped")
