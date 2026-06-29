import json
import time
import wave
import threading
import cv2
from datetime import datetime, timezone

from .queues import (
    event_queue,
    audio_frame_queue,
    upload_queue,
    transcription_queue,
    START_RECORDING,
    STOP_RECORDING,
)
from .session import create_session, write_session_json


FRAME_W = 640
FRAME_H = 480
FPS = 20.0
RATE = 48000
CHANNELS = 2


class RecorderManagerWorker:
    def __init__(self, stop_event, video_frame_queue_ref, recording_flag=None):
        self.stop_event = stop_event
        self.vfq = video_frame_queue_ref
        self.recording_flag = recording_flag or threading.Event()
        self.transcriber = None

    def run(self):
        print("[recorder_manager] started")
        while not self.stop_event.is_set():
            try:
                ev = event_queue.get(timeout=0.2)
            except Exception:
                continue

            ev_name = ev.get("event") if isinstance(ev, dict) else None
            if ev_name == START_RECORDING:
                self._record_session()

        print("[recorder_manager] stopped")

    def _record_session(self):
        session_id, session_dir = create_session()

        if self.transcriber is not None:
            self.transcriber.session_dir = session_dir

        video_path = session_dir / "video.mp4"
        audio_path = session_dir / "audio.wav"
        session_json_path = session_dir / "session.json"

        print(f"[recorder_manager] session started → {session_id}")
        start_time = datetime.now(timezone.utc).isoformat()

        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        writer = cv2.VideoWriter(str(video_path), fourcc, FPS, (FRAME_W, FRAME_H))
        wav_out = wave.open(str(audio_path), "wb")
        wav_out.setnchannels(CHANNELS)
        wav_out.setsampwidth(2)
        wav_out.setframerate(RATE)

        self.recording_flag.set()

        while not audio_frame_queue.empty():
            try:
                audio_frame_queue.get_nowait()
            except Exception:
                break

        while not self.stop_event.is_set():
            while not self.vfq.empty():
                try:
                    item = self.vfq.get_nowait()
                    writer.write(item["frame"])
                except Exception:
                    break

            while not audio_frame_queue.empty():
                try:
                    item = audio_frame_queue.get_nowait()
                    raw = item.get("raw")
                    if raw:
                        wav_out.writeframes(raw)
                except Exception:
                    break

            try:
                ev = event_queue.get_nowait()
                ev_name = ev.get("event") if isinstance(ev, dict) else None
                if ev_name == STOP_RECORDING:
                    break
            except Exception:
                pass

            time.sleep(0.02)

        self.recording_flag.clear()
        end_time = datetime.now(timezone.utc).isoformat()

        writer.release()
        wav_out.close()

        meta = {
            "session_id": session_id,
            "start_time": start_time,
            "end_time": end_time,
            "video_file": "video.mp4",
            "audio_file": "audio.wav",
            "transcript_file": "transcript.txt",
            "merged_video_file": "merged_video.mp4",
            "transcript_status": "pending",
            "merge_status": "pending",
        }
        write_session_json(session_dir, meta)

        upload_queue.put({"session_id": session_id, "file": str(video_path)})
        upload_queue.put({"session_id": session_id, "file": str(audio_path)})
        upload_queue.put({"session_id": session_id, "file": str(session_json_path)})

        def _safe_put(q, item):
            try:
                q.put_nowait(item)
            except Exception:
                try:
                    q.get_nowait()
                except Exception:
                    pass
                try:
                    q.put_nowait(item)
                except Exception:
                    pass

        _safe_put(transcription_queue, None)

        print(f"[recorder_manager] session finalized → {session_id}")