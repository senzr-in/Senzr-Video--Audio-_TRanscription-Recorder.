import time
from pathlib import Path

from .queues import upload_queue

BUCKET = "demoapp-static-files"
REGION = "eu-north-1"
S3_PREFIX = "sessions"
MAX_RETRIES = 3


def _upload_file(path: str, session_id: str):
    try:
        import boto3
        s3 = boto3.client("s3", region_name=REGION)
        key = f"{S3_PREFIX}/{session_id}/{Path(path).name}"
        s3.upload_file(path, BUCKET, key)
        print(f"[uploader] ✓ {Path(path).name} → s3://{BUCKET}/{key}")
    except Exception as e:
        raise RuntimeError(f"upload failed: {e}")


class UploaderWorker:
    def __init__(self, stop_event):
        self.stop_event = stop_event

    def run(self):
        print("[uploader] started")
        while not self.stop_event.is_set():
            try:
                job = upload_queue.get(timeout=0.5)
            except Exception:
                continue

            path = job.get("file") or job.get("path")
            session_id = job.get("session_id", "unknown")

            if not path:
                print("[uploader] invalid job, skipping")
                continue

            for attempt in range(1, MAX_RETRIES + 1):
                try:
                    _upload_file(path, session_id)
                    break
                except Exception as e:
                    print(f"[uploader] attempt {attempt}/{MAX_RETRIES} failed: {e}")
                    if attempt < MAX_RETRIES:
                        time.sleep(2 ** attempt)

        print("[uploader] stopped")