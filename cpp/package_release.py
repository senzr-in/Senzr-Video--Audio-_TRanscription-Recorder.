import os
import sys
import shutil
import zipfile
import hashlib
import json

def build_fota_zip(version="2.4.1"):
    base_dir = os.path.dirname(os.path.abspath(__file__))
    build_dir = os.path.join(base_dir, "build_shared")
    releases_dir = os.path.join(base_dir, "releases")
    os.makedirs(releases_dir, exist_ok=True)

    stage_dir = os.path.join(base_dir, f"_staging_v{version}")
    if os.path.exists(stage_dir):
        shutil.rmtree(stage_dir)

    os.makedirs(os.path.join(stage_dir, "bin"), exist_ok=True)
    os.makedirs(os.path.join(stage_dir, "frontend"), exist_ok=True)
    os.makedirs(os.path.join(stage_dir, "database"), exist_ok=True)
    os.makedirs(os.path.join(stage_dir, "models"), exist_ok=True)

    # Check both build and build_shared directories
    bin_dirs = [os.path.join(base_dir, "build"), os.path.join(base_dir, "build_shared")]

    # 1. Copy binaries (both in bin/ and at root level for dual compatibility)
    for bin_name in ["edge-gateway-backend", "edge-gateway-updater", "senzr-ctl"]:
        for b_dir in bin_dirs:
            src = os.path.join(b_dir, bin_name)
            if os.path.exists(src):
                shutil.copy2(src, os.path.join(stage_dir, "bin", bin_name))
                shutil.copy2(src, os.path.join(stage_dir, bin_name))
                break

    # 2. Copy frontend
    frontend_src = os.path.join(base_dir, "frontend")
    if os.path.exists(frontend_src):
        for item in os.listdir(frontend_src):
            s = os.path.join(frontend_src, item)
            d = os.path.join(stage_dir, "frontend", item)
            if os.path.isfile(s):
                shutil.copy2(s, d)

    # 3. Copy database config
    db_config_src = os.path.join(base_dir, "database", "config.json")
    if not os.path.exists(db_config_src):
        db_config_src = os.path.join(os.path.dirname(base_dir), "database", "config.json")
    if os.path.exists(db_config_src):
        shutil.copy2(db_config_src, os.path.join(stage_dir, "database", "config.json"))

    # Copy updater config if present
    updater_cfg = os.path.join(os.path.dirname(base_dir), "deploy", "updater_config.env")
    if os.path.exists(updater_cfg):
        shutil.copy2(updater_cfg, os.path.join(stage_dir, "updater_config.env"))

    # Copy service scripts
    scripts_src = os.path.join(base_dir, "scripts")
    if os.path.exists(scripts_src):
        os.makedirs(os.path.join(stage_dir, "scripts"), exist_ok=True)
        for item in os.listdir(scripts_src):
            s = os.path.join(scripts_src, item)
            d = os.path.join(stage_dir, "scripts", item)
            if os.path.isfile(s):
                shutil.copy2(s, d)

    # 4. Version metadata
    with open(os.path.join(stage_dir, "version.txt"), "w") as f:
        f.write(f"{version}\n")
    with open(os.path.join(stage_dir, ".version"), "w") as f:
        f.write(f"{version}\n")

    # 5. Build ZIPs (both edge-gateway-v{version}.zip and v{version}.zip)
    zip_name = f"edge-gateway-v{version}.zip"
    zip_path = os.path.join(releases_dir, zip_name)
    alt_zip_path = os.path.join(releases_dir, f"v{version}.zip")

    for p in [zip_path, alt_zip_path]:
        if os.path.exists(p):
            os.remove(p)

    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zipf:
        for root, _, files in os.walk(stage_dir):
            for file in files:
                file_path = os.path.join(root, file)
                arcname = os.path.relpath(file_path, stage_dir)
                zipf.write(file_path, arcname)

    shutil.copy2(zip_path, alt_zip_path)

    # Cleanup staging
    import time
    time.sleep(0.2)
    shutil.rmtree(stage_dir, ignore_errors=True)

    # 6. Calculate SHA-256
    sha256 = hashlib.sha256()
    with open(zip_path, "rb") as f:
        while chunk := f.read(65536):
            sha256.update(chunk)
    checksum = sha256.hexdigest()
    file_size = os.path.getsize(zip_path)

    # 7. Generate latest.json
    manifest = {
        "version": version,
        "file": zip_name,
        "checksum": checksum,
        "size_bytes": file_size,
        "release_notes": f"SenzrOS v{version} Release — Passwordless Direct Web Upload, Auto-Reboot & Live FOTA Telemetry"
    }
    with open(os.path.join(releases_dir, "latest.json"), "w") as f:
        json.dump(manifest, f, indent=2)

    print("======================================================")
    print("  [OK] NEW FOTA RELEASE ZIP CREATED SUCCESSFULLY")
    print("------------------------------------------------------")
    print(f"  Package ZIP : {zip_path}")
    print(f"  File Size   : {file_size:,} bytes")
    print(f"  SHA-256     : {checksum}")
    print(f"  Version     : {version}")
    print(f"  S3 Manifest : {os.path.join(releases_dir, 'latest.json')}")
    print("======================================================")

if __name__ == "__main__":
    ver = sys.argv[1] if len(sys.argv) > 1 else "2.4.1"
    build_fota_zip(ver)
