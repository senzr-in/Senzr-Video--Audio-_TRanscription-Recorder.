from pathlib import Path
import json

BASE_DIR = Path(__file__).resolve().parent.parent.parent
APP_CONFIG_FILE = BASE_DIR / "configs" / "app_config.json"


def get_platform_mode():
    with open(APP_CONFIG_FILE, "r", encoding="utf-8") as f:
        config = json.load(f)
    return config.get("platform_mode", "mock")


def is_mock_mode():
    return get_platform_mode() == "mock"