import json
from pathlib import Path

CONFIG_PATH = Path(__file__).parent / "database" / "config.json"

def readconfig():
    with open(CONFIG_PATH, "r") as file:
        return json.load(file)

def writeconfig(newconfig):
    with open(CONFIG_PATH, "w") as file:
        json.dump(newconfig, file, indent=2)
