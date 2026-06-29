from queue import Queue
import queue
import threading

START_RECORDING = "START_RECORDING"
STOP_RECORDING = "STOP_RECORDING"

video_frame_queue = Queue(maxsize=120)
audio_frame_queue = Queue(maxsize=500)
event_queue = Queue(maxsize=20)
upload_queue = Queue(maxsize=100)

transcription_lock = threading.Lock()
transcription_buffer = []

# NEW
transcription_queue = queue.Queue(maxsize=200)