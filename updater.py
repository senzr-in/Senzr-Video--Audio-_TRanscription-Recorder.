#!/usr/bin/env python3
"""
Edge Gateway OTA Updater
Separate process that manages updates from AWS S3 via MQTT trigger.
Never modifies itself. Controls the edge-gateway systemd service.
"""

import os
import json
import time
import hashlib
import zipfile
import shutil
import logging
import threading
import subprocess
import urllib.request
from pathlib import Path
from datetime import datetime

import boto3
import paho.mqtt.client as mqtt
from dotenv import load_dotenv

# ──────────────────────────────────────────────
# Load config from env file
# ──────────────────────────────────────────────
load_dotenv("/opt/edge-gateway/updater_config.env")

BUCKET          = os.getenv("S3_BUCKET", "demoapp-static-files")
S3_PREFIX       = os.getenv("S3_PREFIX", "app-releases")
MQTT_BROKER     = os.getenv("MQTT_BROKER", "broker.emqx.io")
MQTT_PORT       = int(os.getenv("MQTT_PORT", 1883))
MQTT_TOPIC      = os.getenv("MQTT_TOPIC", "edge-gateway/update")
CURRENT_DIR     = Path(os.getenv("CURRENT_DIR", "/opt/edge-gateway/current"))
BACKUP_DIR      = Path(os.getenv("BACKUP_DIR",  "/opt/edge-gateway/backup"))
DOWNLOADS_DIR   = Path(os.getenv("DOWNLOADS_DIR", "/opt/edge-gateway/downloads"))
CHECK_INTERVAL  = int(os.getenv("CHECK_INTERVAL", 60))
APP_SERVICE     = os.getenv("APP_SERVICE", "edge-gateway")
HEALTH_URL      = os.getenv("HEALTH_CHECK_URL", "http://127.0.0.1:8000/api/status")
LOCK_FILE       = Path("/tmp/edge-gateway-update.lock")
VERSION_FILE    = CURRENT_DIR / ".version"
REQUIRED_FILES  = ["backend/main.py", "frontend/index.html", "requirements.txt"]

# ──────────────────────────────────────────────
# Logging
# ──────────────────────────────────────────────
LOG_DIR = CURRENT_DIR / "logs"
LOG_DIR.mkdir(parents=True, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.FileHandler(LOG_DIR / "updater.log"),
        logging.StreamHandler()
    ]
)
log = logging.getLogger("updater")

# ──────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────

def get_current_version():
    if VERSION_FILE.exists():
        return VERSION_FILE.read_text().strip()
    return "0.0.0"

def set_current_version(version: str):
    VERSION_FILE.write_text(version)

def sha256_of_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()

def acquire_lock() -> bool:
    if LOCK_FILE.exists():
        log.warning("Update already in progress — lock file exists.")
        return False
    LOCK_FILE.write_text(str(os.getpid()))
    return True

def release_lock():
    if LOCK_FILE.exists():
        LOCK_FILE.unlink()

def service_action(action: str):
    log.info(f"systemctl {action} {APP_SERVICE}")
    result = subprocess.run(
        ["systemctl", action, APP_SERVICE],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        log.error(f"systemctl {action} failed: {result.stderr}")
    return result.returncode == 0

def health_check(retries=5, delay=3) -> bool:
    for i in range(retries):
        try:
            with urllib.request.urlopen(HEALTH_URL, timeout=5) as r:
                if r.status == 200:
                    log.info("Health check passed.")
                    return True
        except Exception as e:
            log.warning(f"Health check attempt {i+1}/{retries} failed: {e}")
            time.sleep(delay)
    return False

# ──────────────────────────────────────────────
# S3 Functions
# ──────────────────────────────────────────────

def get_s3_client():
    return boto3.client("s3")

def fetch_latest_metadata() -> dict | None:
    try:
        s3 = get_s3_client()
        key = f"{S3_PREFIX}/latest.json"
        obj = s3.get_object(Bucket=BUCKET, Key=key)
        return json.loads(obj["Body"].read())
    except Exception as e:
        log.error(f"Failed to fetch latest.json from S3: {e}")
        return None

def download_release(file_name: str, dest: Path) -> bool:
    try:
        s3 = get_s3_client()
        key = f"{S3_PREFIX}/{file_name}"
        log.info(f"Downloading s3://{BUCKET}/{key} → {dest}")
        s3.download_file(BUCKET, key, str(dest))
        return True
    except Exception as e:
        log.error(f"Download failed: {e}")
        return False

# ──────────────────────────────────────────────
# Core Update Logic
# ──────────────────────────────────────────────

def run_update(version: str):
    if not acquire_lock():
        return
    log.info(f"Starting OTA update to version {version}")

    meta = fetch_latest_metadata()
    if not meta:
        release_lock()
        return

    zip_name    = meta["file"]
    expected_sha = meta["checksum"]
    zip_path    = DOWNLOADS_DIR / zip_name
    extract_dir = DOWNLOADS_DIR / f"extract_{version}"

    try:
        DOWNLOADS_DIR.mkdir(parents=True, exist_ok=True)

        # 1. Download
        if not download_release(zip_name, zip_path):
            return

        # 2. Checksum
        actual_sha = sha256_of_file(zip_path)
        if actual_sha != expected_sha:
            log.error(f"Checksum mismatch! expected={expected_sha} got={actual_sha}")
            zip_path.unlink(missing_ok=True)
            return
        log.info("Checksum verified.")

        # 3. Extract
        extract_dir.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(zip_path, "r") as z:
            z.extractall(extract_dir)
        log.info(f"Extracted to {extract_dir}")

        # 4. Validate required files
        for req in REQUIRED_FILES:
            if not (extract_dir / req).exists():
                log.error(f"Validation failed: missing {req}")
                shutil.rmtree(extract_dir, ignore_errors=True)
                return
        log.info("Validation passed.")

        # 5. Backup current
        if BACKUP_DIR.exists():
            shutil.rmtree(BACKUP_DIR)
        shutil.copytree(CURRENT_DIR, BACKUP_DIR)
        log.info(f"Backup saved to {BACKUP_DIR}")

        # 6. Stop service
        service_action("stop")
        time.sleep(2)

        # 7. Replace files
        for item in extract_dir.iterdir():
            dest = CURRENT_DIR / item.name
            if dest.is_dir():
                shutil.rmtree(dest)
            elif dest.exists():
                dest.unlink()
            shutil.move(str(item), str(CURRENT_DIR))
        log.info("Files replaced.")

        # 8. Start new version
        service_action("start")
        time.sleep(3)

        # 9. Health check
        if health_check():
            set_current_version(version)
            log.info(f"Update to {version} successful.")
            zip_path.unlink(missing_ok=True)
            shutil.rmtree(extract_dir, ignore_errors=True)
        else:
            log.error("Health check failed. Rolling back.")
            service_action("stop")
            shutil.rmtree(CURRENT_DIR, ignore_errors=True)
            shutil.copytree(BACKUP_DIR, CURRENT_DIR)
            service_action("start")
            log.info("Rollback complete.")

    except Exception as e:
        log.error(f"Update error: {e}")
    finally:
        release_lock()

# ──────────────────────────────────────────────
# Version Check (S3 poll fallback)
# ──────────────────────────────────────────────

def check_for_update():
    meta = fetch_latest_metadata()
    if not meta:
        return
    s3_version = meta.get("version", "0.0.0")
    current = get_current_version()
    log.info(f"Version check — current={current} s3={s3_version}")
    if s3_version != current:
        log.info(f"New version {s3_version} found via S3 poll.")
        run_update(s3_version)

def start_periodic_check():
    def loop():
        while True:
            time.sleep(CHECK_INTERVAL)
            check_for_update()
    t = threading.Thread(target=loop, daemon=True)
    t.start()
    log.info(f"Periodic S3 check every {CHECK_INTERVAL}s started.")

# ──────────────────────────────────────────────
# MQTT
# ──────────────────────────────────────────────

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        log.info(f"MQTT connected to {MQTT_BROKER}")
        client.subscribe(MQTT_TOPIC)
        log.info(f"Subscribed to topic: {MQTT_TOPIC}")
    else:
        log.error(f"MQTT connection failed with code {rc}")

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        log.info(f"MQTT message received: {payload}")
        if payload.get("type") == "update":
            version = payload.get("version", "unknown")
            log.info(f"MQTT triggered update to version {version}")
            threading.Thread(target=run_update, args=(version,), daemon=True).start()
    except Exception as e:
        log.error(f"Failed to process MQTT message: {e}")

def start_mqtt():
    client = mqtt.Client(client_id=f"edge-gateway-{os.getpid()}")
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=60)
    client.loop_start()
    return client

# ──────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────

if __name__ == "__main__":
    log.info("Edge Gateway OTA Updater starting...")
    log.info(f"Current version: {get_current_version()}")
    mqtt_client = start_mqtt()
    start_periodic_check()
    check_for_update()  # immediate check on startup
    log.info("Updater running. Waiting for MQTT or periodic trigger...")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        log.info("Updater stopped.")
