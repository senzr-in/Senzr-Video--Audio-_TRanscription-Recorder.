import subprocess
import time
import threading
import wave
import numpy as np

from .queues import audio_frame_queue

ALSA_DEVICE   = "plughw:2,0"
CHANNELS      = 2
RATE          = 48000
FORMAT        = "S16_LE"
CHUNK_SECS    = 0.5   # emit 0.5s chunks for near-real-time transcription


class AudioCaptureWorker:
    def __init__(self, stop_event):
        self.stop_event = stop_event

    def run(self):
        chunk_frames = int(RATE * CHUNK_SECS)
        cmd = [
            "arecord",
            "-D", ALSA_DEVICE,
            "-c", str(CHANNELS),
            "-r", str(RATE),
            "-f", FORMAT,
            "-t", "raw",
            "--quiet",
        ]
        print("[audio_capture] started")
        try:
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            bytes_per_frame = CHANNELS * 2  # S16_LE = 2 bytes per sample
            chunk_bytes = chunk_frames * bytes_per_frame

            while not self.stop_event.is_set():
                raw = proc.stdout.read(chunk_bytes)
                if not raw:
                    time.sleep(0.05)
                    continue
                samples = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
                item = {"timestamp": time.time(), "audio_chunk": samples, "raw": raw}
                try:
                    audio_frame_queue.put_nowait(item)
                except Exception:
                    pass

        except Exception as e:
            print(f"[audio_capture] ERROR: {e}")
        finally:
            try:
                proc.terminate()
            except Exception:
                pass
        print("[audio_capture] stopped")
