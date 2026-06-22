import boto3
from pathlib import Path

BUCKET_NAME = "demoapp-static-files"

s3 = boto3.client("s3")


def upload_session(session_id: str, session_dir: str):
    session_dir = Path(session_dir)

    for file_path in session_dir.iterdir():

        if not file_path.is_file():
            continue

        key = f"{session_id}/{file_path.name}"

        print(
            f"[S3] Uploading "
            f"{file_path} -> s3://{BUCKET_NAME}/{key}"
        )

        s3.upload_file(
            str(file_path),
            BUCKET_NAME,
            key
        )

    print(f"[S3] Session uploaded: {session_id}")
