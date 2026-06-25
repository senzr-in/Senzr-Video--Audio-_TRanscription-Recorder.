import threading
import json
from pathlib import Path

import numpy as np
import whisper


SAMPLE_RATE = 48000
CHANNELS = 2
WINDOW_SEC = 5
OVERLAP_SEC = 2
WINDOW_FRAMES = SAMPLE_RATE * WINDOW_SEC
OVERLAP_FRAMES = SAMPLE_RATE * OVERLAP_SEC


class StreamingTranscriber(threading.Thread):
    def __init__(self, transcription_queue, merge_queue, upload_queue, session_dir):
        super().__init__(daemon=True)
        self.tq = transcription_queue
        self.mq = merge_queue
        self.uq = upload_queue
        self.session_dir = Path(session_dir) if session_dir else None
        self.model = whisper.load_model("tiny")
        self._full_text = ""

    def set_session_dir(self, session_dir):
        self.session_dir = Path(session_dir)

    def run(self):
        while self.session_dir is None:
            if self.tq.empty():
                continue
            try:
                peek = self.tq.get(timeout=0.5)
                self.tq.put(peek)
            except Exception:
                continue

        buffer = np.array([], dtype=np.int16)
        transcript_path = self.session_dir / "transcript.txt"
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

        session_json = self.session_dir / "session.json"
        if session_json.exists():
            self._update_session_json(session_json, "completed")
        else:
            meta = {"transcript_status": "completed", "transcript_file": "transcript.txt"}
            session_json.write_text(json.dumps(meta, indent=2), encoding="utf-8")

        self.uq.put({"session_id": self.session_dir.name, "file": str(transcript_path)})
        self.uq.put({"session_id": self.session_dir.name, "file": str(session_json)})
        self.mq.put({"session_dir": str(self.session_dir)})

    def _process_window(self, pcm, transcript_path):
        audio_f32 = pcm.astype(np.float32) / 32768.0
        if CHANNELS == 2:
            audio_f32 = audio_f32.reshape(-1, 2).mean(axis=1)

        result = self.model.transcribe(
            audio_f32,
            language="en",
            initial_prompt=self._full_text[-200:] if self._full_text else None
        )
        new_text = result.get("text", "").strip()
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