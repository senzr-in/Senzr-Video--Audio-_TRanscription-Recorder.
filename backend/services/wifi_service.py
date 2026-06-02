import sys
from pathlib import Path

# Add project root to sys.path so mocks/ is always findable
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from mocks.wifi import (
    get_status,
    sync_from_config,
    start_ap,
    stop_ap,
    set_uplink,
)


def get_wifi_status():
    return get_status()


def start_access_point():
    sync_from_config()   # Pull latest SSID/channel from app_config.json first
    return start_ap()    # Then activate the AP


def stop_access_point():
    return stop_ap()


def update_uplink(connected: bool):
    return set_uplink(connected)