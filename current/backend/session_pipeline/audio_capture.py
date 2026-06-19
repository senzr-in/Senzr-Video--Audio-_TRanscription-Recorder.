import subprocess
import threading
from pathlib import Path

from .config import AUDIO_DEVICE, AUDIO_CHANNELS, AUDIO_RATE, FFMPEG_BIN


class AudioCaptureWorker:
    def __init__(self, out_path: Path, stop_event: threading.Event):
        self.out_path = Path(out_path)
        self.stop_event = stop_event
        self.proc = None

    def run(self):
        cmd = [
            FFMPEG_BIN,
            "-y",
            "-f", "alsa",
            "-ac", str(AUDIO_CHANNELS),
            "-ar", str(AUDIO_RATE),
            "-i", AUDIO_DEVICE,
            "-c:a", "pcm_s16le",
            str(self.out_path),
        ]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        while not self.stop_event.is_set():
            if self.proc.poll() is not None:
                break
            self.stop_event.wait(0.2)
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
