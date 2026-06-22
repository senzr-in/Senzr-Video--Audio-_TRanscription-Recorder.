import queue
import threading

from session_pipeline.video_capture import VideoCapture
from session_pipeline.audio_capture import AudioCapture
from session_pipeline.person_detection import PersonDetection
from session_pipeline.recorder_manager import RecorderManager
from session_pipeline.transcriber import Transcriber
from session_pipeline.uploader import S3Uploader
from session_pipeline.queues import video_frame_queue


class SessionPipeline:
    """
    Manages all 6 worker threads.
    Caller calls .start() and .stop() — that's it.
    """

    def __init__(self):
        self._stop_event = threading.Event()
        self._event_queue = queue.Queue()
        self._threads: list[threading.Thread] = []

        # Instantiate workers
        self._video_capture   = VideoCapture()
        self._audio_capture   = AudioCapture()
        self._person_detection = PersonDetection(self._event_queue)
        self._recorder_manager = RecorderManager(self._event_queue, video_frame_queue)
        self._transcriber     = Transcriber()
        self._uploader        = S3Uploader()

    def _make_thread(self, name: str, worker_run):
        t = threading.Thread(
            target=worker_run,
            args=(self._stop_event,),
            name=name,
            daemon=True,
        )
        return t

    def start(self):
        if self._threads:
            print("[Pipeline] Already running")
            return

        self._stop_event.clear()
        self._threads = [
            self._make_thread("video-capture",    self._video_capture.run),
            self._make_thread("audio-capture",    self._audio_capture.run),
            self._make_thread("person-detection", self._person_detection.run),
            self._make_thread("recorder-manager", self._recorder_manager.run),
            self._make_thread("transcriber",      self._transcriber.run),
            self._make_thread("s3-uploader",      self._uploader.run),
        ]

        for t in self._threads:
            t.start()
            print(f"[Pipeline] Started thread: {t.name}")

        print("[Pipeline] All 6 workers running")

    def stop(self):
        print("[Pipeline] Stop signal sent")
        self._stop_event.set()

        for t in self._threads:
            t.join(timeout=15)
            if t.is_alive():
                print(f"[Pipeline] WARNING: {t.name} did not stop in time")

        self._threads.clear()
        print("[Pipeline] All workers stopped")

    @property
    def is_running(self) -> bool:
        return any(t.is_alive() for t in self._threads)
