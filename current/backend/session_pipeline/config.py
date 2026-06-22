from pathlib import Path

# Paths
BASE_DIR = Path("/opt/edge-gateway")
LOCAL_STORAGE = BASE_DIR / "local_storage"
MODEL_PATH = BASE_DIR / "current/backend/models/yolov8n.rknn"

# Camera
CAMERA_INDEX = 0
FRAME_W = 640
FRAME_H = 480
MODEL_INPUT_SIZE = 640
CAMERA_FPS = 20.0

# Detection
PERSON_CLASS_ID = 0
OBJ_THRESH = 0.30
EARLY_OBJ_THRESH = 0.15
NMS_THRESH = 0.45
START_THRESHOLD = 5       # consecutive frames with person to start
GRACE_PERIOD_SEC = 10.0   # seconds person absent before stopping

# Audio
AUDIO_DEVICE = "plughw:2,0"
AUDIO_CHANNELS = 2
AUDIO_RATE = 48000
AUDIO_FORMAT = "S16_LE"

# AWS S3
AWS_ACCESS_KEY_ID = os.getenv("AWS_ACCESS_KEY_ID")
AWS_SECRET_ACCESS_KEY = os.getenv("AWS_SECRET_ACCESS_KEY")
AWS_REGION = "ap-south-1"
S3_BUCKET = "demoapp-static-files"

# Queue sizes (0 = unlimited)
VIDEO_QUEUE_SIZE = 5
AUDIO_QUEUE_SIZE = 100
TRANSCRIPTION_QUEUE_SIZE = 50
UPLOAD_QUEUE_SIZE = 200

# Upload retry
UPLOAD_MAX_RETRIES = 3
UPLOAD_RETRY_DELAY = 5
