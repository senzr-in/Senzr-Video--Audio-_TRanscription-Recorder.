import json
import queue
import threading
from pathlib import Path

import whisper

from session_pipeline.queues import transcription_queue, upload_queue

WHISPER_MODEL = "tiny"
_model = None
_model_lock = threading.Lock()


def _get_model():
    global _model
    with _model_lock:
        if _model is None:
            print(f"[Transcriber] Loading Whisper model: {WHISPER_MODEL}")
            _model = whisper.load_model(WHISPER_MODEL)
            print("[Transcriber] Whisper model loaded")
    return _model


def _transcribe_audio(audio_path: str) -> str:
    model = _get_model()
    result = model.transcribe(audio_path, fp16=False)
    return result.get("text", "").strip()


def _update_session_json(session_dir: str, session_id: str, transcript: str):
    path = Path(session_dir) / "session.json"
    if path.exists():
        data = json.loads(path.read_text())
    else:
        data = {"session_id": session_id}
    data["transcript_status"] = "completed"
    data["transcript_preview"] = transcript[:200]
    path.write_text(json.dumps(data, indent=2))
    return str(path)


class Transcriber:
    def run(self, stop_event: threading.Event):
        print("[Transcriber] Starting")
        while not stop_event.is_set():
            try:
                job = transcription_queue.get(timeout=1.0)
            except queue.Empty:
                continue

            session_id = job["session_id"]
            session_dir = job["session_dir"]
            audio_path = job["audio_path"]
            print(f"[Transcriber] Transcribing {session_id}")

            try:
                transcript = _transcribe_audio(audio_path)

                # Write transcript.txt
                txt_path = Path(session_dir) / "transcript.txt"
                txt_path.write_text(transcript)
                print(f"[Transcriber] Done -> {txt_path}")

                # Update session.json
                json_path = _update_session_json(session_dir, session_id, transcript)

                # Enqueue transcript + updated session.json for upload
                upload_queue.put({"session_id": session_id, "file_path": str(txt_path)})
                upload_queue.put({"session_id": session_id, "file_path": json_path})

            except Exception as e:
                print(f"[Transcriber] Error for {session_id}: {e}")

        print("[Transcriber] Stopped")
