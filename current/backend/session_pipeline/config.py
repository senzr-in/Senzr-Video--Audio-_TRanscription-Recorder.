from pathlib import Path

BASE_DIR = Path(__file__).resolve().parents[1]
MODEL_PATH = BASE_DIR / "models" / "yolov8n.rknn"
RECORDINGS_DIR = Path("/opt/edge-gateway/recordings")
RECORDINGS_DIR.mkdir(parents=True, exist_ok=True)

CAMERA_INDEX = 0
FRAME_WIDTH = 640
FRAME_HEIGHT = 480
MODEL_INPUT_SIZE = 640

PERSON_CLASS_ID = 0
OBJ_THRESH = 0.30
EARLY_OBJ_THRESH = 0.15
NMS_THRESH = 0.45
START_CONFIRM_FRAMES = 5
STOP_CONFIRM_FRAMES = 40
DEBUG_EVERY_N_FRAMES = 10
