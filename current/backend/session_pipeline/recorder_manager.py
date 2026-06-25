import json
import time
import wave
import threading
import cv2
import numpy as np
from pathlib import Path
from datetime import datetime, timezone

from .queues import (
    event_queue, audio_frame_queue, upload_queue,
    START_RECORDING, STOP_RECORDING
)
from .session import create_session, write_session_json

FRAME_W  = 640
FRAME_H  = 480
FPS      = 20.0
RATE     = 48000
CHANNELS = 2


class RecorderManagerWorker:
    def __init__(self, stop_event, video_frame_queue_ref, transcriber_ref):
        self.stop_event      = stop_event
        self.vfq             = video_frame_queue_ref
        self.transcriber     = transcriber_ref
        self.recording_flag  = threading.Event()  # shared with TranscriberWorker

    def run(self):
        print("[recorder_manager] started")
        while not self.stop_event.is_set():
            try:
                ev = event_queue.get(timeout=0.2)
            except Exception:
                continue

            if ev["event"] == START_RECORDING:
                self._record_session()

        print("[recorder_manager] stopped")

    def _record_session(self):
        session_id, session_dir = create_session()
        video_path = session_dir / "video.mp4"
        audio_path = session_dir / "audio.wav"

        print(f"[recorder_manager] session started → {session_id}")
        start_time = datetime.now(timezone.utc).isoformat()

        fourcc  = cv2.VideoWriter_fourcc(*"mp4v")
        writer  = cv2.VideoWriter(str(video_path), fourcc, FPS, (FRAME_W, FRAME_H))
        wav_out = wave.open(str(audio_path), "wb")
        wav_out.setnchannels(CHANNELS)
        wav_out.setsampwidth(2)
        wav_out.setframerate(RATE)

        self.recording_flag.set()

        # drain any stale audio
        while not audio_frame_queue.empty():
            try:
                audio_frame_queue.get_nowait()
            except Exception:
                break

        audio_buffer = []

        while not self.stop_event.is_set():
            # write video frames
            while not self.vfq.empty():
                try:
                    item = self.vfq.get_nowait()
                    writer.write(item["frame"])
                except Exception:
                    break

            # write audio chunks to wav
            while not audio_frame_queue.empty():
                try:
                    item = audio_frame_queue.get_nowait()
                    raw  = item.get("raw")
                    if raw:
                        wav_out.writeframes(raw)
                except Exception:
                    break

            # check for stop event
            try:
                ev = event_queue.get_nowait()
                if ev["event"] == STOP_RECORDING:
                    break
            except Exception:
                pass

            time.sleep(0.02)

        self.recording_flag.clear()
        end_time = datetime.now(timezone.utc).isoformat()

        writer.release()
        wav_out.close()

        # get transcript from transcriber
        transcript_path = self.transcriber.finalize(session_dir, session_id)

        # write session.json
        meta = {
            "session_id":        session_id,
            "start_time":        start_time,
            "end_time":          end_time,
            "video_file":        "video.mp4",
            "audio_file":        "audio.wav",
            "transcript_file":   "transcript.txt",
            "transcript_status": "completed",
        }
        session_json_path = write_session_json(session_dir, meta)

        # push everything to upload_queue
        for path in [str(video_path), str(audio_path), str(session_json_path)]:
            upload_queue.put({"path": path, "session_id": session_id})

        print(f"[recorder_manager] session finalized → {session_id}")
