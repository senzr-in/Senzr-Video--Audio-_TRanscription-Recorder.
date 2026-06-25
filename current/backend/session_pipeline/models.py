from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Optional


def utc_now_iso():
    return datetime.utcnow().isoformat() + "Z"


@dataclass
class SessionInfo:
    session_id: str
    session_dir: Path
    start_time: str = field(default_factory=utc_now_iso)
    end_time: Optional[str] = None
    duration_seconds: Optional[float] = None
    video_file: str = "video.mp4"
    audio_file: str = "audio.wav"
    transcript_file: Optional[str] = None
    transcript_status: str = "pending"
    upload_status: str = "pending"

    def to_dict(self):
        return {
            "session_id": self.session_id,
            "session_dir": str(self.session_dir),
            "start_time": self.start_time,
            "end_time": self.end_time,
            "duration_seconds": self.duration_seconds,
            "video_file": self.video_file,
            "audio_file": self.audio_file,
            "transcript_file": self.transcript_file,
            "transcript_status": self.transcript_status,
            "upload_status": self.upload_status,
        }
