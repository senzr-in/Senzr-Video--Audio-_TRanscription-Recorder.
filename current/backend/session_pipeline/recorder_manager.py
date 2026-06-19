# current/backend/session_pipeline/recorder_manager.py
import json
import threading
import time
from datetime import datetime
from pathlib import Path
import wave
import subprocess

import cv2

from .config import (
    LOCAL_SESSIONS_ROOT,
    FRAME_WIDTH,
    FRAME_HEIGHT,
    VIDEO_FPS,
    AUDIO_SAMPLE_RATE,
)
from .queues import (
    recording_event_queue,
    video_frame_queue,
    audio_frame_queue,
    transcription_queue,
    upload_queue,
    stop_event,
)
from .models import (
    SessionMeta,
    new_session_id,
    RecordingEvent,
    TranscriptionJob,
    UploadJob,
)


class RecorderManager:
    def __init__(self):
        self._lock = threading.Lock()
        self._session: SessionMeta | None = None
        self._video_writer = None
        self._audio_wave: wave.Wave_write | None = None
        self._recording_active = False

    def _s3_key(self, session_id: str, filename: str) -> str:
        from .config import S3_SESSIONS_PREFIX

        return f"{S3_SESSIONS_PREFIX}/{session_id}/{filename}"

    def _mux_audio_video(self, session_dir: Path, session_id: str):
        """
        Use ffmpeg to create session_av.mp4 from video.mp4 + audio.wav
        """
        video = session_dir / "video.mp4"
        audio = session_dir / "audio.wav"
        output = session_dir / "session_av.mp4"

        if not video.exists() or not audio.exists():
            print(f"[REC] Skipping mux: missing video or audio for {session_id}")
            return

        cmd = [
            "ffmpeg",
            "-y",
            "-i",
            str(video),
            "-i",
            str(audio),
            "-c:v",
            "copy",
            "-c:a",
            "aac",
            "-shortest",
            str(output),
        ]
        print(f"[REC] Muxing AV for {session_id}: {' '.join(cmd)}")
        try:
            subprocess.run(cmd, check=False)
        except Exception as e:
            print(f"[REC] ffmpeg mux failed for {session_id}: {e}")
            return

        if output.exists():
            print(f"[REC] Created combined file: {output}")
            # enqueue upload of combined A/V file
            upload_queue.put(
                UploadJob(
                    local_path=output,
                    s3_key=self._s3_key(session_id, "session_av.mp4"),
                )
            )

    def _start_new_session(self, ts: float):
        session_id = new_session_id()
        session_dir = LOCAL_SESSIONS_ROOT / session_id
        session_dir.mkdir(parents=True, exist_ok=True)

        start_time = datetime.utcfromtimestamp(ts)
        video_path = session_dir / "video.mp4"
        audio_path = session_dir / "audio.wav"
        session_json_path = session_dir / "session.json"

        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        vw = cv2.VideoWriter(str(video_path), fourcc, VIDEO_FPS, (FRAME_WIDTH, FRAME_HEIGHT))

        aw = wave.open(str(audio_path), "wb")
        aw.setnchannels(1)
        aw.setsampwidth(2)  # 16-bit audio
        aw.setframerate(AUDIO_SAMPLE_RATE)

        self._session = SessionMeta(
            session_id=session_id,
            start_time=start_time,
            video_path=video_path,
            audio_path=audio_path,
        )
        self._video_writer = vw
        self._audio_wave = aw
        self._recording_active = True

        # initial session.json
        session_json = {
            "session_id": session_id,
            "start_time": start_time.isoformat() + "Z",
            "end_time": None,
            "duration_seconds": None,
            "video_file": "video.mp4",
            "audio_file": "audio.wav",
            # transcript fields will be filled later
            "transcript_status": "processing",
        }
        with open(session_json_path, "w") as f:
            json.dump(session_json, f, indent=2)

        # immediate upload tasks for video/audio/session.json
        upload_queue.put(
            UploadJob(local_path=video_path, s3_key=self._s3_key(session_id, "video.mp4"))
        )
        upload_queue.put(
            UploadJob(local_path=audio_path, s3_key=self._s3_key(session_id, "audio.wav"))
        )
        upload_queue.put(
            UploadJob(
                local_path=session_json_path,
                s3_key=self._s3_key(session_id, "session.json"),
            )
        )

        print(f"[REC] Session started: {session_id}")

    def _stop_session(self, ts: float):
        if not self._session:
            return

        end_time = datetime.utcfromtimestamp(ts)
        self._session.end_time = end_time

        if self._video_writer is not None:
            self._video_writer.release()
            self._video_writer = None
        if self._audio_wave is not None:
            self._audio_wave.close()
            self._audio_wave = None

        session_dir = self._session.video_path.parent
        session_json_path = session_dir / "session.json"

        # update session.json with end_time + duration
        with open(session_json_path, "r") as f:
            meta = json.load(f)
        meta["end_time"] = end_time.isoformat() + "Z"
        meta["duration_seconds"] = self._session.duration_seconds
        with open(session_json_path, "w") as f:
            json.dump(meta, f, indent=2)

        # enqueue transcription job (audio only)
        transcription_queue.put(
            TranscriptionJob(
                session_id=self._session.session_id,
                audio_path=self._session.audio_path,
            )
        )

        # run ffmpeg mux to create combined A/V file
        self._mux_audio_video(session_dir, self._session.session_id)

        self._recording_active = False
        print(f"[REC] Session stopped: {self._session.session_id}")

        # keep session meta if you want; transcriber updates session.json later

    def handle_event(self, ev: RecordingEvent):
        with self._lock:
            if ev.type == "START_RECORDING":
                if not self._recording_active:
                    self._start_new_session(ev.timestamp)
            elif ev.type == "STOP_RECORDING":
                if self._recording_active:
                    self._stop_session(ev.timestamp)

    def handle_video_frame(self, frame):
        with self._lock:
            if self._recording_active and self._video_writer is not None:
                self._video_writer.write(frame)

    def handle_audio_chunk(self, data: bytes):
        with self._lock:
            if self._recording_active and self._audio_wave is not None:
                self._audio_wave.writeframes(data)


def recorder_manager_loop():
    mgr = RecorderManager()
    print("[REC] Recorder manager thread started")

    while not stop_event.is_set():
        # 1) handle recording events
        try:
            ev = recording_event_queue.get(timeout=0.05)
            mgr.handle_event(ev)
        except Exception:
            pass

        # 2) handle queued video frames
        try:
            vf = video_frame_queue.get_nowait()
            mgr.handle_video_frame(vf.frame)
        except Exception:
            pass

        # 3) handle queued audio chunks
        try:
            ac = audio_frame_queue.get_nowait()
            mgr.handle_audio_chunk(ac.data)
        except Exception:
            pass

        time.sleep(0.005)

    print("[REC] Recorder manager thread exiting")
