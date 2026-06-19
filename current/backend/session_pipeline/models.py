# current/backend/session_pipeline/models.py
from dataclasses import dataclass
from pathlib import Path
from typing import Literal, Optional
import numpy as np
import time
from datetime import datetime


@dataclass
class VideoFrame:
    timestamp: float
    frame: np.ndarray


@dataclass
class AudioChunk:
    timestamp: float
    data: bytes


RecordingEventType = Literal["START_RECORDING", "STOP_RECORDING"]


@dataclass
class RecordingEvent:
    type: RecordingEventType
    timestamp: float


@dataclass
class TranscriptionJob:
    session_id: str
    audio_path: Path


@dataclass
class UploadJob:
    local_path: Path
    s3_key: str


@dataclass
class SessionMeta:
    session_id: str
    start_time: datetime
    end_time: Optional[datetime] = None
    video_path: Optional[Path] = None
    audio_path: Optional[Path] = None
    transcript_path: Optional[Path] = None

    @property
    def duration_seconds(self) -> Optional[int]:
        if not self.end_time:
            return None
        return int((self.end_time - self.start_time).total_seconds())


def new_session_id() -> str:
    ts = datetime.utcnow().strftime("%Y%m%d_%H%M%S")
    return f"session_{ts}"


def now_ts() -> float:
    return time.time()
