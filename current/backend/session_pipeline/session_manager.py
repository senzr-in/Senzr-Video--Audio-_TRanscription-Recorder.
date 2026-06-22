from datetime import datetime
from pathlib import Path


RECORDINGS_ROOT = Path("/opt/edge-gateway/current/recordings")


class SessionManager:

    def __init__(self):
        self.session_id = None
        self.session_dir = None

    def create_session(self):

        self.session_id = datetime.now().strftime("%y%m%d_%H%M%S")

        self.session_dir = RECORDINGS_ROOT / self.session_id

        self.session_dir.mkdir(
            parents=True,
            exist_ok=True
        )

        return self.session_id, self.session_dir

    def get_video_path(self):

        return self.session_dir / "video.mp4"

    def get_audio_path(self):

        return self.session_dir / "audio.wav"

    def get_transcript_path(self):

        return self.session_dir / "transcript.json"
