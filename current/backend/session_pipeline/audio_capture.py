import time
import math
import wave
import pyaudio  # make sure it's in requirements

from .config import (
    AUDIO_SAMPLE_RATE,
    AUDIO_CHANNELS,
    AUDIO_CHUNK_SECONDS,
)
from .queues import audio_frame_queue, stop_event
from .models import AudioChunk, now_ts


def audio_capture_loop():
    print("[AUDIO] Capture loop starting")

    pa = pyaudio.PyAudio()
    frames_per_buffer = int(AUDIO_SAMPLE_RATE * AUDIO_CHUNK_SECONDS)

    stream = pa.open(
        format=pyaudio.paInt16,
        channels=AUDIO_CHANNELS,
        rate=AUDIO_SAMPLE_RATE,
        input=True,
        frames_per_buffer=frames_per_buffer,
    )

    try:
        while not stop_event.is_set():
            data = stream.read(frames_per_buffer, exception_on_overflow=False)
            ts = now_ts()
            chunk = AudioChunk(timestamp=ts, data=data)
            try:
                audio_frame_queue.put(chunk, timeout=0.1)
            except Exception:
                # Queue full; drop audio chunk
                pass
    finally:
        stream.stop_stream()
        stream.close()
        pa.terminate()
        print("[AUDIO] Capture loop stopped, mic released")