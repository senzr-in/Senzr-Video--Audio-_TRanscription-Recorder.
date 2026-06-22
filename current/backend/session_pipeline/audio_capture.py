import subprocess
import threading
import time
from pathlib import Path


class AudioRecorder:
    DEVICE = "hw:2,0"
    SAMPLE_RATE = 16000
    CHANNELS = 2
    FORMAT = "S16_LE"

    def __init__(self, output_file):
        self.output_file = str(output_file)
        self.process = None
        self.lock = threading.Lock()

    def start(self):
        with self.lock:
            if self.process is not None:
                return

            Path(self.output_file).parent.mkdir(
                parents=True,
                exist_ok=True
            )

            cmd = [
                "/usr/bin/arecord",
                "-D", self.DEVICE,
                "-f", self.FORMAT,
                "-r", str(self.SAMPLE_RATE),
                "-c", str(self.CHANNELS),
                self.output_file
            ]

            print("[AUDIO CMD]", " ".join(cmd))

            self.process = subprocess.Popen(cmd)

            time.sleep(1)

            rc = self.process.poll()

            print("[AUDIO RC]", rc)

            print(f"[AUDIO] Started -> {self.output_file}")

    def stop(self):
        with self.lock:
            if self.process is None:
                return

            self.process.terminate()

            try:
                self.process.wait(timeout=5)
            except Exception:
                self.process.kill()

            self.process = None

            print(f"[AUDIO] Stopped -> {self.output_file}")
