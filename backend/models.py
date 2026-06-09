from pydantic import BaseModel


class ConfigModel(BaseModel):
    device_name: str
    mode: str
    wifi_ssid: str
    wifi_password: str
    provisioning_enabled: bool
    camera_connected: bool