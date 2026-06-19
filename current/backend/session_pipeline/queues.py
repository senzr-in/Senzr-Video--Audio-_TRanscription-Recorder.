# current/backend/session_pipeline/queues.py
import threading
from queue import Queue

from .models import VideoFrame, AudioChunk, RecordingEvent, TranscriptionJob, UploadJob

# Shared stop event
stop_event = threading.Event()

# Queues
video_frame_queue: "Queue[VideoFrame]" = Queue(maxsize=10)
audio_frame_queue: "Queue[AudioChunk]" = Queue(maxsize=50)
recording_event_queue: "Queue[RecordingEvent]" = Queue(maxsize=20)
transcription_queue: "Queue[TranscriptionJob]" = Queue(maxsize=10)
upload_queue: "Queue[UploadJob]" = Queue(maxsize=100)
