import boto3
import os
from pathlib import Path
from dotenv import load_dotenv

load_dotenv("/opt/edge-gateway/updaterconfig.env")

AWS_BUCKET     = os.getenv("AWS_BUCKET_NAME", "demoapp-static-files")
AWS_REGION     = os.getenv("AWS_REGION", "ap-south-1")
AWS_KEY_ID     = os.getenv("AWS_ACCESS_KEY_ID")
AWS_SECRET_KEY = os.getenv("AWS_SECRET_ACCESS_KEY")


def upload_recording(local_path: Path):
    """Upload a completed recording to S3 under recordings/ prefix."""
    if not AWS_KEY_ID or not AWS_SECRET_KEY:
        print("[S3] AWS credentials not set — skipping upload")
        return
    try:
        s3 = boto3.client(
            "s3",
            region_name=AWS_REGION,
            aws_access_key_id=AWS_KEY_ID,
            aws_secret_access_key=AWS_SECRET_KEY,
        )
        key = f"recordings/{local_path.name}"
        print(f"[S3] Uploading {local_path.name} → s3://{AWS_BUCKET}/{key}")
        s3.upload_file(str(local_path), AWS_BUCKET, key)
        print(f"[S3] Upload complete: {key}")
    except Exception as e:
        print(f"[S3] Upload failed: {e}")
