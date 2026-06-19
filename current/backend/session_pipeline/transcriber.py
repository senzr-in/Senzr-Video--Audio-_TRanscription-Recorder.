# current/backend/session_pipeline/transcriber.py
# Temporary stub: no real Whisper. Marks transcript as completed with placeholder text.

import json

from .queues import transcription_queue, upload_queue, stop_event
from .models import TranscriptionJob, UploadJob
from .config import S3_SESSIONS_PREFIX


def transcriber_loop():
    print("[WHISPER-STUB] Transcriber stub thread starting")

    while not stop_event.is_set():
        try:
            job: TranscriptionJob = transcription_queue.get(timeout=0.1)
        except Exception:
            continue

        session_id = job.session_id
        audio_path = job.audio_path
        session_dir = audio_path.parent
        transcript_path = session_dir / "transcript.txt"
        session_json_path = session_dir / "session.json"

        print(f"[WHISPER-STUB] Marking transcript for session {session_id}")

        # Placeholder transcript text
        text = "(transcription disabled in this build)"

        # Write transcript.txt
        with open(transcript_path, "w") as f:
            f.write(text)

        # Update session.json
        with open(session_json_path, "r") as f:
            meta = json.load(f)
        meta["transcript_file"] = "transcript.txt"
        meta["transcript"] = text
        meta["transcript_status"] = "completed"
        with open(session_json_path, "w") as f:
            json.dump(meta, f, indent=2)

        # Queue uploads for transcript + updated session.json
        upload_queue.put(
            UploadJob(
                local_path=transcript_path,
                s3_key=f"{S3_SESSIONS_PREFIX}/{session_id}/transcript.txt",
            )
        )
        upload_queue.put(
            UploadJob(
                local_path=session_json_path,
                s3_key=f"{S3_SESSIONS_PREFIX}/{session_id}/session.json",
            )
        )

        print(f"[WHISPER-STUB] Completed session {session_id}")
