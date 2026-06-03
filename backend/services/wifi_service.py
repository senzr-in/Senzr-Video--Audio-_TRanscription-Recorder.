from pathlib import Path
from typing import Tuple
import json

CONFIG_PATH = Path(__file__).parent.parent.parent / "configs" / "device-config.json"

HOSTAPD_PATH = Path(__file__).parent.parent.parent / "configs" / "hostapd.conf"
DNSMASQ_PATH = Path(__file__).parent.parent.parent / "configs" / "dnsmasq.conf"


def read_config() -> dict:
    if CONFIG_PATH.exists():
        return json.loads(CONFIG_PATH.read_text())
    return {}


def render_hostapd(cfg: dict) -> str:
    ssid = cfg.get("wifi_ssid", "")
    password = cfg.get("wifi_password", "")
    if not ssid:
        raise ValueError("SSID is required for AP")
    if len(password) < 8:
        raise ValueError("Password must be at least 8 characters")

    return "\n".join(
        [
            "interface=wlan0",
            "driver=nl80211",
            "ssid=" + ssid,
            "hw_mode=g",
            "channel=6",
            "wmm_enabled=1",
            "macaddr_acl=0",
            "auth_algs=1",
            "ignore_broadcast_ssid=0",
            "wpa=2",
            "wpa_key_mgmt=WPA-PSK",
            "wpa_pairwise=TKIP",
            "rsn_pairwise=CCMP",
            "wpa_passphrase=" + password,
            "",
        ]
    )


def render_dnsmasq(cfg: dict) -> str:
    return "\n".join(
        [
            "interface=wlan0",
            "dhcp-range=192.168.4.2,192.168.4.20,255.255.255.0,24h",
            "domain-needed",
            "bogus-priv",
            "no-resolv",
            "",
        ]
    )


def write_wifi_configs() -> Tuple[bool, str]:
    cfg = read_config()
    try:
        hostapd_text = render_hostapd(cfg)
        dnsmasq_text = render_dnsmasq(cfg)
    except ValueError as exc:
        return False, str(exc)

    HOSTAPD_PATH.write_text(hostapd_text)
    DNSMASQ_PATH.write_text(dnsmasq_text)
    return True, "hostapd.conf and dnsmasq.conf written under configs/"


def apply_wifi_settings() -> Tuple[bool, str]:
    ok, msg = write_wifi_configs()
    if not ok:
        return False, msg
    # later: copy these to /etc/hostapd/hostapd.conf and /etc/dnsmasq.d/edge-ap.conf on the Pi
    return True, msg


def restart_wifi_services() -> Tuple[bool, str]:
    # later: run systemctl restart hostapd dnsmasq on the Pi
    return True, "Wi-Fi services restart simulated"