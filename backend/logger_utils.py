import json
from pathlib import Path
from datetime import datetime

LOG_PATH = Path("logs/changes.log")
LEASE_FILE = Path("/var/lib/misc/dnsmasq.leases")


def ensure_log_dir():
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)


def write_file_log(entry: dict):
    ensure_log_dir()
    with open(LOG_PATH, "a") as file:
        file.write(json.dumps(entry) + "\n")


def get_timestamp():
    return datetime.utcnow().isoformat() + "Z"


def diff_configs(old_config: dict, new_config: dict):
    changed = []
    for key in new_config.keys():
        if old_config.get(key) != new_config.get(key):
            changed.append(key)
    return changed


def get_mac_from_ip(client_ip: str):
    if not LEASE_FILE.exists():
        return None

    try:
        with open(LEASE_FILE, "r") as file:
            for line in file:
                parts = line.strip().split()
                if len(parts) >= 3:
                    lease_mac = parts[1]
                    lease_ip = parts[2]
                    if lease_ip == client_ip:
                        return lease_mac
    except Exception:
        return None

    return None