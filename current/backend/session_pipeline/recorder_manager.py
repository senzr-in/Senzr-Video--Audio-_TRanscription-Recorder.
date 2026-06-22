import json
import queue
import threading
import wave
import struct
import time
from datetime import datetime, timezone
from pathlib import Path

import cv2

from session_pipeline.config import (
    LOCAL_STORAGE, FRAME_W, FRAME_H, CAMERA_FPS,
    AUDIO_CHANNELS, AUDIO_RATE, AUDIO_FORMAT,
)
from session_pipeline.queues import audio_frame_queue, transcription_queue, upload_queue
from session_pipeline.person_detection import PersonDetection


class RecorderManager:
    """
    Sole owner of session state.
    Listens for START_RECORDING / STOP_RECORDING from event_queue.
    Drains audio_frame_queue and writes audio.wav.
    Receives video frames pushed by caller (run loop).
    """

    def __init__(self, event_queue: queue.Queue, video_frame_queue_ref):
        self._event_queue = event_queue
        self._video_frame_queue = video_frame_queue_ref

        self._session_dir: Path = None
        self._session_id: str = None
        self._video_writer = None
        self._wav_file = None
        self._recording = False
        self._start_time: str = None

        LOCAL_STORAGE.mkdir(parents=True, exist_ok=True)

    # ── session helpers ──────────────────────────────────────────────────────

    def _create_session(self):
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        self._session_id = f"session_{ts}"
        self._session_dir = LOCAL_STORAGE / self._session_id
        self._session_dir.mkdir(parents=True, exist_ok=True)
        self._start_time = datetime.now(timezone.utc).isoformat()
        print(f"[RecorderManager] Session folder: {self._session_dir}")

    def _init_video_writer(self):
        path = self._session_dir / "video.mp4"
        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        self._video_writer = cv2.VideoWriter(str(path), fourcc, CAMERA_FPS, (FRAME_W, FRAME_H))
        if not self._video_writer.isOpened():
            raise RuntimeError(f"VideoWriter failed: {path}")
        return path

    def _init_wav_writer(self):
        path = self._session_dir / "audio.wav"
        self._wav_file = wave.open(str(path), "wb")
        self._wav_file.setnchannels(AUDIO_CHANNELS)
        self._wav_file.setsampwidth(2)   # S16_LE = 2 bytes
        self._wav_file.setframerate(AUDIO_RATE)
        return path

    def _write_session_json(self, status="processing", extra=None):
        data = {
            "session_id": self._session_id,
            "start_time": self._start_time,
            "transcript_status": status,
        }
        if extra:
            data.update(extra)
        path = self._session_dir / "session.json"
        path.write_text(json.dumps(data, indent=2))
        return path

    def _drain_audio_queue(self):
        while True:
            try:
                item = audio_frame_queue.get_nowait()
                if self._wav_file:
                    self._wav_file.writeframes(item["audio_chunk"])
            except queue.Empty:
                break

    # ── start / stop ─────────────────────────────────────────────────────────

    def _on_start(self):
        self._create_session()
        video_path = self._init_video_writer()
        audio_path = self._init_wav_writer()
        json_path  = self._write_session_json(status="processing")
        self._recording = True
        print(f"[RecorderManager] Recording STARTED — {self._session_id}")
        print(f"  video: {video_path}")
        print(f"  audio: {audio_path}")
        print(f"  meta:  {json_path}")

    def _on_stop(self):
        self._recording = False

        # Drain remaining audio
        self._drain_audio_queue()

        # Finalise video
        if self._video_writer:
            self._video_writer.release()
            self._video_writer = None

        # Finalise audio
        if self._wav_file:
            self._wav_file.close()
            self._wav_file = None

        end_time = datetime.now(timezone.utc).isoformat()
        video_path = self._session_dir / "video.mp4"
        audio_path = self._session_dir / "audio.wav"
        json_path  = self._write_session_json(
            status="processing",
            extra={"end_time": end_time},
        )

        print(f"[RecorderManager] Recording STOPPED — {self._session_id}")

        # Enqueue uploads (video + audio + session.json)
        for f in [video_path, audio_path, json_path]:
            upload_queue.put({"session_id": self._session_id, "file_path": str(f)})

        # Enqueue transcription
        transcription_queue.put({
            "session_id": self._session_id,
            "session_dir": str(self._session_dir),
            "audio_path": str(audio_path),
        })

        print(f"[RecorderManager] Queued uploads + transcription for {self._session_id}")

    # ── main run loop ─────────────────────────────────────────────────────────

    def run(self, stop_event: threading.Event):
        print("[RecorderManager] Starting")
        while not stop_event.is_set():
            # Handle events (non-blocking)
            try:
                event = self._event_queue.get_nowait()
                if event == PersonDetection.START_EVENT and not self._recording:
                    self._on_start()
                elif event == PersonDetection.STOP_EVENT and self._recording:
                    self._on_stop()
            except queue.Empty:
                pass

            # Write video frames from video_frame_queue during active session
            if self._recording and self._video_writer:
                try:
                    item = self._video_frame_queue.get_nowait()
                    self._video_writer.write(item["frame"])
                except queue.Empty:
                    pass

            # Write audio chunks continuously
            if self._recording and self._wav_file:
                self._drain_audio_queue()

            time.sleep(0.001)

        # Cleanup on shutdown
        if self._recording:
            print("[RecorderManager] Stop signal during active session — finalising")
            self._on_stop()

        print("[RecorderManager] Stopped")
