import json
import shutil
import threading
import subprocess
from datetime import datetime
from pathlib import Path

from .config import SESSION_BASE_DIR, FFMPEG_BIN
from .models import SessionMeta
from .transcriber import Transcriber
from .uploader import Uploader


class RecorderManager:
    def __init__(self, uploader=None):
        self.uploader = uploader or Uploader()
        self.transcriber = Transcriber()
        self.lock = threading.Lock()
        self.current_session = None
        self.session_dir = None
        self.video_path = None
        self.audio_path = None

    def _new_session_id(self):
        return datetime.now().strftime("session_%Y%m%d_%H%M%S")

    def start_session(self):
        with self.lock:
            if self.current_session is not None:
                return self.current_session

            session_id = self._new_session_id()
            session_dir = SESSION_BASE_DIR / session_id
            session_dir.mkdir(parents=True, exist_ok=True)

            self.session_dir = session_dir
            self.video_path = session_dir / "video.mp4"
            self.audio_path = session_dir / "audio.wav"

            meta = SessionMeta(
                session_id=session_id,
                session_dir=str(session_dir),
                started_at=datetime.utcnow().isoformat() + "Z",
            )
            self.current_session = meta
            self._write_session_json(meta)
            return meta

    def _write_session_json(self, meta: SessionMeta):
        (Path(meta.session_dir) / "session.json").write_text(
            json.dumps(meta.to_dict(), indent=2),
            encoding="utf-8",
        )

    def finalize_session(self, started_at=None):
        with self.lock:
            if self.current_session is None or self.session_dir is None:
                return None

            meta = self.current_session
            meta.ended_at = datetime.utcnow().isoformat() + "Z"
            meta.status = "finalizing"

            self._write_session_json(meta)

            av_path = self.session_dir / "session_av.mp4"
            if self.video_path.exists() and self.audio_path.exists():
                cmd = [
                    FFMPEG_BIN,
                    "-y",
                    "-i", str(self.video_path),
                    "-i", str(self.audio_path),
                    "-c:v", "copy",
                    "-c:a", "aac",
                    "-shortest",
                    str(av_path),
                ]
                subprocess.run(cmd, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

            transcript = self.transcriber.transcribe(self.session_dir)
            meta.transcript_status = "done"
            meta.status = "complete"

            self._write_session_json(meta)

            if self.uploader:
                self.uploader.upload_session(self.session_dir)

            self.current_session = None
            self.session_dir = None
            self.video_path = None
            self.audio_path = None

            return transcript
