from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SESSION_BASE_DIR = Path("/opt/edge-gateway/recordings")
SESSION_BASE_DIR.mkdir(parents=True, exist_ok=True)

CAMERA_INDEX = 0
FRAME_W = 640
FRAME_H = 480
FRAME_RATE = 20.0

AUDIO_RATE = 16000
AUDIO_CHANNELS = 1
AUDIO_DEVICE = "hw:2,0"

ALSA_DEVICE = AUDIO_DEVICE
FFMPEG_BIN = "ffmpeg"
