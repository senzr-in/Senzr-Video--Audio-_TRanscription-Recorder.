# current/backend/session_pipeline/audio_capture.py
import time
import pyaudio

from .config import (
    AUDIO_SAMPLE_RATE,
    AUDIO_CHANNELS,
    AUDIO_CHUNK_SECONDS,
)
from .queues import audio_frame_queue, stop_event
from .models import AudioChunk, now_ts

ONBOARD_DEVICE_INDEX = 2  # from your PyAudio test


def audio_capture_loop():
    print("[AUDIO] Capture loop starting (on-board mic, device index 2)")

    pa = pyaudio.PyAudio()
    frames_per_buffer = int(AUDIO_SAMPLE_RATE * AUDIO_CHUNK_SECONDS)

    info = pa.get_device_info_by_host_api_device_index(0, ONBOARD_DEVICE_INDEX)
    print(
        f"[AUDIO] Using device {ONBOARD_DEVICE_INDEX}: "
        f"name={info.get('name')}, maxInput={info.get('maxInputChannels')}, "
        f"defaultSR={info.get('defaultSampleRate')}"
    )

    stream = pa.open(
        format=pyaudio.paInt16,
        channels=AUDIO_CHANNELS,
        rate=AUDIO_SAMPLE_RATE,
        input=True,
        input_device_index=ONBOARD_DEVICE_INDEX,
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
                pass
    finally:
        stream.stop_stream()
        stream.close()
        pa.terminate()
        print("[AUDIO] Capture loop stopped, on-board mic released")
