# current/backend/session_pipeline/uploader.py
import time
import boto3
from botocore.exceptions import BotoCoreError, ClientError

from .queues import upload_queue, stop_event
from .config import AWS_BUCKET, AWS_REGION


def uploader_loop():
    print("[UPLOAD] S3 uploader thread starting")

    session = boto3.Session(region_name=AWS_REGION)
    s3 = session.client("s3")

    while not stop_event.is_set():
        try:
            job = upload_queue.get(timeout=0.1)
        except Exception:
            continue

        local_path = job.local_path
        key = job.s3_key

        print(f"[UPLOAD] Uploading {local_path} -> s3://{AWS_BUCKET}/{key}")
        while not stop_event.is_set():
            try:
                s3.upload_file(str(local_path), AWS_BUCKET, key)
                print(f"[UPLOAD] Success {local_path}")
                break
            except (BotoCoreError, ClientError) as e:
                print(f"[UPLOAD] Failed {local_path}: {e}. Retrying in 5s")
                time.sleep(5)
