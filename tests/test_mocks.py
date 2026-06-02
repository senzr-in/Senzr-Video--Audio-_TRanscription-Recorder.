from mocks.wifi import (
    get_status as wifi_status,
    sync_from_config as wifi_sync,
    start_ap,
    connect_client,
    set_uplink,
)
from mocks.camera import (
    get_status as camera_status,
    sync_from_config as camera_sync,
    set_mode,
    start_inference,
    capture_frame,
    run_inference,
)

print("=== WIFI ===")
print(wifi_sync())
print(start_ap())
print(connect_client("phone-1"))
print(set_uplink(True))
print(wifi_status())

print("\n=== CAMERA ===")
print(camera_sync())
print(set_mode("object"))
print(start_inference())
print(capture_frame())
print(run_inference())
print(camera_status())