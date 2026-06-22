import os
import signal
import subprocess
import threading
import queue
import time
from datetime import datetime

from session_pipeline.config import (
    AUDIO_DEVICE, AUDIO_CHANNELS, AUDIO_RATE, AUDIO_FORMAT,
)
from session_pipeline.queues import audio_frame_queue

CHUNK_DURATION_MS = 100
BYTES_PER_SAMPLE = 2
CHUNK_SIZE = int(AUDIO_RATE * AUDIO_CHANNELS * BYTES_PER_SAMPLE * CHUNK_DURATION_MS / 1000)


class AudioCapture:
    def __init__(self):
        self._proc = None

    def run(self, stop_event: threading.Event):
        print("[AudioCapture] Starting")

        cmd = [
            "arecord",
            "-D", AUDIO_DEVICE,
            "-c", str(AUDIO_CHANNELS),
            "-r", str(AUDIO_RATE),
            "-f", AUDIO_FORMAT,
            "-t", "raw",
            "--buffer-size=8192",
            "-",
        ]

        self._proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )

        try:
            while not stop_event.is_set():
                chunk = self._proc.stdout.read(CHUNK_SIZE)
                if not chunk:
                    time.sleep(0.01)
                    continue

                item = {
                    "timestamp": datetime.utcnow().isoformat(),
                    "audio_chunk": chunk,
                    "sample_rate": AUDIO_RATE,
                    "channels": AUDIO_CHANNELS,
                    "format": AUDIO_FORMAT,
                }

                try:
                    audio_frame_queue.put_nowait(item)
                except queue.Full:
                    try:
                        audio_frame_queue.get_nowait()
                    except queue.Empty:
                        pass
                    audio_frame_queue.put_nowait(item)

        finally:
            if self._proc and self._proc.poll() is None:
                try:
                    os.killpg(os.getpgid(self._proc.pid), signal.SIGTERM)
                except ProcessLookupError:
                    pass
            print("[AudioCapture] arecord stopped")
