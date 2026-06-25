import threading
import json
from pathlib import Path

from .queues import transcription_queue, upload_queue

_model = None
_model_lock = threading.Lock()


def _get_model():
    global _model
    with _model_lock:
        if _model is None:
            from faster_whisper import WhisperModel
            _model = WhisperModel("small", device="cpu", compute_type="int8")
    return _model


class TranscriberWorker:
    def __init__(self, stop_event, recording_flag):
        self.stop_event = stop_event
        self.recording_flag = recording_flag

    def run(self):
        print("[transcriber] started")
        while not self.stop_event.is_set():
            try:
                job = transcription_queue.get(timeout=0.5)
            except Exception:
                continue

            try:
                self.process_job(job)
            finally:
                transcription_queue.task_done()

        print("[transcriber] stopped")

    def process_job(self, job):
        session_dir = Path(job["session_dir"])
        audio_path = Path(job["audio_path"])
        session_id = job["session_id"]

        model = _get_model()
        segments, _ = model.transcribe(str(audio_path), language=None, vad_filter=True)
        text = " ".join(seg.text.strip() for seg in segments).strip()

        transcript_path = session_dir / "transcript.txt"
        transcript_path.write_text(text, encoding="utf-8")

        session_json = session_dir / "session.json"
        if session_json.exists():
            data = json.loads(session_json.read_text(encoding="utf-8"))
            data["transcript_file"] = "transcript.txt"
            data["transcript_status"] = "completed"
            session_json.write_text(json.dumps(data, indent=2), encoding="utf-8")

        upload_queue.put({"path": str(transcript_path), "session_id": session_id})
        if session_json.exists():
            upload_queue.put({"path": str(session_json), "session_id": session_id})
