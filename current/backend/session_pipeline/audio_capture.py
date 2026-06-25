import subprocess
import time
import threading
import queue

from .queues import audio_frame_queue, transcription_queue


ALSA_DEVICE = "plughw:2,0"
CHANNELS = 2
RATE = 48000
FORMAT = "S16_LE"
CHUNK_SECS = 0.5  # emit 0.5s chunks for near-real-time transcription


class AudioCaptureWorker:
    def __init__(self, stop_event, recording_event=None):
        self.stop_event = stop_event
        self.recording_event = recording_event or threading.Event()

    @property
    def recording_active(self):
        return self.recording_event.is_set()

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
        proc = None
        try:
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            bytes_per_frame = CHANNELS * 2
            chunk_bytes = chunk_frames * bytes_per_frame

            while not self.stop_event.is_set():
                raw = proc.stdout.read(chunk_bytes)
                if not raw:
                    time.sleep(0.05)
                    continue

                item = {"timestamp": time.time(), "raw": raw}

                try:
                    audio_frame_queue.put_nowait(item)
                except queue.Full:
                    try:
                        audio_frame_queue.get_nowait()
                    except queue.Empty:
                        pass
                    try:
                        audio_frame_queue.put_nowait(item)
                    except queue.Full:
                        pass

                if self.recording_active:
                    try:
                        transcription_queue.put_nowait(item)
                    except queue.Full:
                        try:
                            transcription_queue.get_nowait()
                        except queue.Empty:
                            pass
                        try:
                            transcription_queue.put_nowait(item)
                        except queue.Full:
                            pass

        except Exception as e:
            print(f"[audio_capture] ERROR: {e}")
        finally:
            try:
                if proc is not None:
                    proc.terminate()
            except Exception:
                pass
        print("[audio_capture] stopped")
