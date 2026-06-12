import json
from pathlib import Path

CONFIG_PATH = Path("database/config.json")


def read_config():
    with open(CONFIG_PATH, "r") as file:
        return json.load(file)


def write_config(new_config):
    with open(CONFIG_PATH, "w") as file:
        json.dump(new_config, file, indent=2)