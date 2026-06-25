import json
import time
from datetime import datetime, timezone
from pathlib import Path

RECORDINGS_DIR = Path("/opt/edge-gateway/recordings")
RECORDINGS_DIR.mkdir(parents=True, exist_ok=True)


def create_session() -> tuple[str, Path]:
    session_id = "session_" + datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    session_dir = RECORDINGS_DIR / session_id
    session_dir.mkdir(parents=True, exist_ok=True)
    return session_id, session_dir


def write_session_json(session_dir: Path, data: dict):
    p = session_dir / "session.json"
    p.write_text(json.dumps(data, indent=2), encoding="utf-8")
    return p


def read_session_json(session_dir: Path) -> dict:
    p = session_dir / "session.json"
    return json.loads(p.read_text(encoding="utf-8"))
