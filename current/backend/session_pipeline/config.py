from pathlib import Path

# Local filesystem layout
BASE_DIR = Path("/opt/edge-gateway")
LOCAL_SESSIONS_ROOT = BASE_DIR / "recordings"  # reuse your existing path
LOCAL_SESSIONS_ROOT.mkdir(parents=True, exist_ok=True)

# Camera and audio
CAMERA_INDEX = 0
FRAME_WIDTH = 640
FRAME_HEIGHT = 480
VIDEO_FPS = 20.0

AUDIO_SAMPLE_RATE = 16000
AUDIO_CHANNELS = 1
AUDIO_CHUNK_SECONDS = 0.5  # ~0.5s per chunk

# Detection stability logic (frames)
START_CONFIRM_FRAMES = 3
STOP_ABSENT_SECONDS = 2.0  # person absent for 2 seconds -> stop recording

# AWS S3
AWS_REGION = "ap-south-1"  # update if needed
AWS_BUCKET = "demoapp-static-files"  # fill from AWS-info.txt

S3_SESSIONS_PREFIX = "sessions"

# Whisper
WHISPER_MODEL_NAME = "tiny"  # or path to Tiny model; you can adjust later