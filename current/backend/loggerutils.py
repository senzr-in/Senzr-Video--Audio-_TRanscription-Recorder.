import json
from pathlib import Path
from datetime import datetime

LOG_PATH = Path(__file__).parent.parent / "logs" / "changes.log"
LEASE_FILE = Path("/var/lib/misc/dnsmasq.leases")

def ensurelogdir():
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)

def writefilelog(entry: dict):
    ensurelogdir()
    with open(LOG_PATH, "a") as file:
        file.write(json.dumps(entry) + "\n")

def gettimestamp():
    return datetime.utcnow().isoformat() + "Z"

def diffconfigs(oldconfig: dict, newconfig: dict):
    changed = []
    for key in newconfig.keys():
        if oldconfig.get(key) != newconfig.get(key):
            changed.append(key)
    return changed

def getmacfromip(clientip: str):
    if not LEASE_FILE.exists():
        return None
    try:
        with open(LEASE_FILE, "r") as file:
            for line in file:
                parts = line.strip().split()
                if len(parts) >= 3:
                    lease_mac = parts[1]
                    lease_ip = parts[2]
                    if lease_ip == clientip:
                        return lease_mac
    except Exception:
        return None
    return None
