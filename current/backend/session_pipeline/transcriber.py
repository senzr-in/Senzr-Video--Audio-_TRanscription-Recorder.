import threading
import json
from pathlib import Path

import numpy as np
from faster_whisper import WhisperModel

from .queues import transcription_queue, upload_queue


SAMPLE_RATE = 48000
CHANNELS = 2
WINDOW_SEC = 5
OVERLAP_SEC = 2
WINDOW_FRAMES = SAMPLE_RATE * WINDOW_SEC
OVERLAP_FRAMES = SAMPLE_RATE * OVERLAP_SEC


class StreamingTranscriber(threading.Thread):
    def __init__(self, transcription_queue, upload_queue, session_dir):
        super().__init__(daemon=True)
        self.tq = transcription_queue
        self.uq = upload_queue
        self.session_dir = Path(session_dir) if session_dir else None
        self._full_text = ""
        self.model = None

    def set_session_dir(self, session_dir):
        self.session_dir = Path(session_dir)

    def _load_model(self):
        if self.model is not None:
            return self.model

        self.model = WhisperModel(
            "tiny",
            device="cpu",
            compute_type="int8"
        )

        return self.model

    def run(self):
        buffer = np.array([], dtype=np.int16)
        session_dir = self.session_dir

        while session_dir is None:
            try:
                session_dir = self.session_dir
                if session_dir is None:
                    continue
            except Exception:
                continue

        transcript_path = session_dir / "transcript.txt"
        transcript_path.parent.mkdir(parents=True, exist_ok=True)
        transcript_path.write_text("", encoding="utf-8")

        while True:
            chunk = self.tq.get()
            if chunk is None:
                self._flush(buffer, transcript_path)
                break

            raw = chunk.get("raw") if isinstance(chunk, dict) else chunk
            if raw is None:
                continue

            arr = np.frombuffer(raw, dtype=np.int16)
            buffer = np.concatenate([buffer, arr])

            if len(buffer) >= WINDOW_FRAMES * CHANNELS:
                self._process_window(buffer[:WINDOW_FRAMES * CHANNELS], transcript_path)
                buffer = buffer[OVERLAP_FRAMES * CHANNELS:]

        session_json = session_dir / "session.json"
        if session_json.exists():
            self._update_session_json(session_json, "completed")
        else:
            meta = {"transcript_status": "completed", "transcript_file": "transcript.txt"}
            session_json.write_text(json.dumps(meta, indent=2), encoding="utf-8")

        self.uq.put({
            "session_id": session_dir.name,
            "file": str(transcript_path)
        })

        self.uq.put({
            "session_id": session_dir.name,
            "file": str(session_json)
        })

    def _process_window(self, pcm, transcript_path):
        model = self._load_model()
        audio_f32 = pcm.astype(np.float32) / 32768.0
        if CHANNELS == 2:
            audio_f32 = audio_f32.reshape(-1, 2).mean(axis=1)

        segments, info = model.transcribe(
            audio_f32,
            language="en",
            initial_prompt=self._full_text[-200:] if self._full_text else None,
            vad_filter=False
        )

        new_text = " ".join(segment.text for segment in segments).strip()
        deduplicated = self._deduplicate(self._full_text, new_text)
        if deduplicated:
            self._full_text = (self._full_text + " " + deduplicated).strip()
            with open(transcript_path, "a", encoding="utf-8") as f:
                f.write(deduplicated + " ")

    def _flush(self, buffer, transcript_path):
        if len(buffer) > CHANNELS * SAMPLE_RATE // 2:
            self._process_window(buffer, transcript_path)

    def _deduplicate(self, existing, new_text):
        if not existing:
            return new_text
        words_existing = existing.split()
        words_new = new_text.split()
        for i in range(min(10, len(words_existing), len(words_new)), 0, -1):
            if words_existing[-i:] == words_new[:i]:
                return " ".join(words_new[i:])
        return new_text

    def _update_session_json(self, path, status):
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception:
            data = {}
        data["transcript_status"] = status
        data["transcript_file"] = "transcript.txt"
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)