from pydantic import BaseModel

class ConfigModel(BaseModel):
    devicename: str
    mode: str
    wifissid: str
    wifipassword: str
    provisioningenabled: bool
    cameraconnected: bool
