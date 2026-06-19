# current/backend/session_pipeline/config.py
from pathlib import Path

# Local filesystem layout
BASE_DIR = Path("/opt/edge-gateway")
LOCAL_SESSIONS_ROOT = BASE_DIR / "recordings"
LOCAL_SESSIONS_ROOT.mkdir(parents=True, exist_ok=True)

# Camera and audio
FRAME_WIDTH = 640
FRAME_HEIGHT = 480
VIDEO_FPS = 20.0

AUDIO_SAMPLE_RATE = 16000
AUDIO_CHANNELS = 1
AUDIO_CHUNK_SECONDS = 0.5  # ~0.5s per chunk

# Detection stability logic
START_CONFIRM_FRAMES = 3
STOP_ABSENT_SECONDS = 2.0  # person absent for 2s -> STOP_RECORDING

# AWS S3
AWS_REGION = "ap-south-1"  # adjust if needed
AWS_BUCKET = "demoapp-static-files"  # put your real bucket

S3_SESSIONS_PREFIX = "sessions"

# Whisper
WHISPER_MODEL_NAME = "tiny"
