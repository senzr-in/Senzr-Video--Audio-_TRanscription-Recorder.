from pathlib import Path
from datetime import datetime
from .config import RECORDINGS_DIR
from .models import SessionInfo


class SessionManager:
    def __init__(self, base_dir: Path = RECORDINGS_DIR):
        self.base_dir = Path(base_dir)
        self.session: SessionInfo | None = None

    def create_session(self):
        session_id = datetime.utcnow().strftime("session_%Y%m%d_%H%M%S")
        session_dir = self.base_dir / session_id
        session_dir.mkdir(parents=True, exist_ok=True)
        self.session = SessionInfo(session_id=session_id, session_dir=session_dir)
        return self.session, session_dir

    def get_video_path(self):
        if not self.session:
            raise RuntimeError("Session not created")
        return self.session.session_dir / self.session.video_file

    def get_audio_path(self):
        if not self.session:
            raise RuntimeError("Session not created")
        return self.session.session_dir / self.session.audio_file

    def get_session_json_path(self):
        if not self.session:
            raise RuntimeError("Session not created")
        return self.session.session_dir / "session.json"
