import queue
from session_pipeline.config import (
    VIDEO_QUEUE_SIZE,
    AUDIO_QUEUE_SIZE,
    TRANSCRIPTION_QUEUE_SIZE,
    UPLOAD_QUEUE_SIZE,
)

video_frame_queue = queue.Queue(maxsize=VIDEO_QUEUE_SIZE)
audio_frame_queue = queue.Queue(maxsize=AUDIO_QUEUE_SIZE)
transcription_queue = queue.Queue(maxsize=TRANSCRIPTION_QUEUE_SIZE)
upload_queue = queue.Queue(maxsize=UPLOAD_QUEUE_SIZE)
