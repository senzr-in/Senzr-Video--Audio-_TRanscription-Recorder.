import queue
import threading
import time
from pathlib import Path

import boto3
from botocore.exceptions import BotoCoreError, ClientError

from session_pipeline.config import (
    AWS_ACCESS_KEY, AWS_SECRET_KEY, AWS_REGION,
    S3_BUCKET, UPLOAD_MAX_RETRIES, UPLOAD_RETRY_DELAY,
)
from session_pipeline.queues import upload_queue


def _build_s3_key(session_id: str, file_path: str) -> str:
    filename = Path(file_path).name
    return f"sessions/{session_id}/{filename}"


def _upload_file(s3_client, session_id: str, file_path: str) -> bool:
    key = _build_s3_key(session_id, file_path)
    for attempt in range(1, UPLOAD_MAX_RETRIES + 1):
        try:
            s3_client.upload_file(file_path, S3_BUCKET, key)
            print(f"[Uploader] Uploaded s3://{S3_BUCKET}/{key}")
            return True
        except (BotoCoreError, ClientError) as e:
            print(f"[Uploader] Attempt {attempt}/{UPLOAD_MAX_RETRIES} failed for {file_path}: {e}")
            if attempt < UPLOAD_MAX_RETRIES:
                time.sleep(UPLOAD_RETRY_DELAY)
    print(f"[Uploader] All retries exhausted for {file_path}")
    return False


class S3Uploader:
    def run(self, stop_event: threading.Event):
        print("[Uploader] Starting")
        s3 = boto3.client(
            "s3",
            region_name=AWS_REGION,
            aws_access_key_id=AWS_ACCESS_KEY,
            aws_secret_access_key=AWS_SECRET_KEY,
        )

        while not stop_event.is_set():
            try:
                job = upload_queue.get(timeout=1.0)
            except queue.Empty:
                continue

            session_id = job["session_id"]
            file_path = job["file_path"]

            if not Path(file_path).exists():
                print(f"[Uploader] File not found, skipping: {file_path}")
                continue

            _upload_file(s3, session_id, file_path)

        # Drain remaining on shutdown
        print("[Uploader] Draining remaining upload queue on shutdown...")
        while True:
            try:
                job = upload_queue.get_nowait()
                _upload_file(s3, job["session_id"], job["file_path"])
            except queue.Empty:
                break

        print("[Uploader] Stopped")
