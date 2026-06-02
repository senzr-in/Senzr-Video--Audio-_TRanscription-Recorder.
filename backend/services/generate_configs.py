from pathlib import Path
import json

BASE_DIR = Path(__file__).resolve().parent.parent
CONFIG_FILE = BASE_DIR / "configs" / "app_config.json"
TEMPLATES_DIR = BASE_DIR / "configs" / "templates"
OUTPUT_DIR = BASE_DIR / "configs" / "generated"

OUTPUT_DIR.mkdir(exist_ok=True)


def load_config():
    with open(CONFIG_FILE, "r", encoding="utf-8") as f:
        return json.load(f)


def render_template(template_name, replacements):
    template_path = TEMPLATES_DIR / template_name
    content = template_path.read_text(encoding="utf-8")
    for key, value in replacements.items():
        content = content.replace(f"{{{{{key}}}}}", str(value))
    return content


def write_output(filename, content):
    (OUTPUT_DIR / filename).write_text(content, encoding="utf-8")


def main():
    config = load_config()

    replacements = {
        "FLASK_PORT": 5000,
        "WIFI_INTERFACE": "wlan0",
        "SSID": config["wifi"]["ssid"],
        "PASSWORD": config["wifi"]["password"],
        "CHANNEL": config["wifi"]["channel"],
        "AP_IP": "10.0.0.1",
        "DHCP_START": "10.0.0.10",
        "DHCP_END": "10.0.0.200"
    }

    write_output(
        "nginx-edge-gateway.conf",
        render_template("nginx-edge-gateway.conf.template", replacements)
    )
    write_output(
        "hostapd.conf",
        render_template("hostapd.conf.template", replacements)
    )
    write_output(
        "dnsmasq.conf",
        render_template("dnsmasq.conf.template", replacements)
    )

    print("Generated config files in configs/generated/")


if __name__ == "__main__":
    main()