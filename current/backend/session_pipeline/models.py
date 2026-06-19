from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Optional
from datetime import datetime


@dataclass
class SessionMeta:
    session_id: str
    session_dir: str
    started_at: str
    ended_at: Optional[str] = None
    video_file: str = "video.mp4"
    audio_file: str = "audio.wav"
    session_av_file: str = "session_av.mp4"
    transcript_file: str = "transcript.txt"
    status: str = "recording"
    transcript_status: str = "pending"
    upload_status: str = "pending"

    def to_dict(self):
        return asdict(self)
