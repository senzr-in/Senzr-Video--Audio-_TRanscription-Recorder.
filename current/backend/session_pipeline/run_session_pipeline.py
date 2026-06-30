import threading

from .queues import (
    detect_frame_queue,
    record_frame_queue,
    event_queue,
    upload_queue,
    transcription_queue,
)
from .video_capture import VideoCaptureWorker
from .audio_capture import AudioCaptureWorker
from .person_detection import PersonDetectionWorker
from .recorder_manager import RecorderManagerWorker
from .transcriber import StreamingTranscriber
from .uploader import UploaderWorker


class SessionPipeline:
    def __init__(self):
        self._stop_event = threading.Event()
        self._threads = []
        self._recording_flag = threading.Event()
        self.is_running = False

        self.video_worker = None
        self.audio_worker = None
        self.detect_worker = None
        self.recorder = None
        self.transcriber = None
        self.uploader = None

    def start(self):
        if self.is_running:
            return

        self._stop_event.clear()

        self.video_worker = VideoCaptureWorker(self._stop_event)
        self.audio_worker = AudioCaptureWorker(self._stop_event, self._recording_flag)
        self.detect_worker = PersonDetectionWorker(self._stop_event)
        self.transcriber = StreamingTranscriber(
            transcription_queue,
            upload_queue,
            None,
        )
        self.recorder = RecorderManagerWorker(
            self._stop_event,
            record_frame_queue,
            event_queue,
            self._recording_flag
        )
        self.recorder.transcriber = self.transcriber

        self.uploader = UploaderWorker(self._stop_event)

        workers = [
            self.video_worker,
            self.audio_worker,
            self.detect_worker,
            self.recorder,
            self.transcriber,
            self.uploader,
        ]

        self._threads = []
        for w in workers:
            t = threading.Thread(target=w.run, daemon=True)
            t.start()
            self._threads.append(t)

        self.is_running = True
        print("[pipeline] all workers started")

    def stop(self):
        if not self.is_running:
            return

        self._stop_event.set()
        transcription_queue.put(None)

        for t in self._threads:
            t.join(timeout=5)

        self._threads.clear()
        self.is_running = False
        print("[pipeline] all workers stopped")


def main():
    pipeline = SessionPipeline()
    pipeline.start()
    return pipeline


if __name__ == "__main__":
    pipeline = main()