#!/usr/bin/env python3
"""
SENZR Edge Gateway - Physical BLE & REST Access Subsystem Test Client
Validates Path 2 Physical Hardware Integration:
  - GATT Service UUID:       9F82A001-7B34-4C12-8921-5092A1B2C3D4
  - Challenge Nonce Char:    9F82A002-7B34-4C12-8921-5092A1B2C3D4 (Read / Notify)
  - Response / Unlock Char:  9F82A003-7B34-4C12-8921-5092A1B2C3D4 (Write with Response)
  - Door Status Char:        9F82A004-7B34-4C12-8921-5092A1B2C3D4 (Read / Notify)
  - Provisioned Secret Key:  senzr_sec_k992_prod_human_gate_master
  - Proximity Threshold:     Phone within 3m (RSSI >= -78 dBm)
"""

import sys
import os
import hmac
import hashlib
import json
import time
import argparse

SERVICE_UUID     = "9F82A001-7B34-4C12-8921-5092A1B2C3D4".lower()
CHALLENGE_UUID   = "9F82A002-7B34-4C12-8921-5092A1B2C3D4".lower()
RESPONSE_UUID    = "9F82A003-7B34-4C12-8921-5092A1B2C3D4".lower()
DOOR_STATUS_UUID = "9F82A004-7B34-4C12-8921-5092A1B2C3D4".lower()
DEFAULT_SECRET   = "senzr_sec_k992_prod_human_gate_master"
PROXIMITY_RSSI   = -78

def compute_hmac_sha256(secret_key: str, nonce: str) -> str:
    """Calculates HMAC-SHA256 hex digest of nonce using provisioned secret key."""
    return hmac.new(
        secret_key.encode("utf-8"),
        nonce.encode("utf-8"),
        hashlib.sha256
    ).hexdigest()

def test_via_rest(gateway_url: str, secret_key: str, rssi: int = -68):
    """Executes Path 2 Verification against Edge Gateway REST / Simulation API."""
    import urllib.request
    import urllib.error

    print(f"\n=======================================================")
    print(f"  SENZR PHYSICAL GATEWAY VERIFICATION (REST / LOOPBACK)")
    print(f"  Target: {gateway_url}")
    print(f"  Secret: {secret_key}")
    print(f"  RSSI  : {rssi} dBm (Threshold >= {PROXIMITY_RSSI} dBm)")
    print(f"=======================================================\n")

    # Step 1: Health & Config Check
    print("[1/5] Querying /api/ble/config ...")
    try:
        req = urllib.request.Request(f"{gateway_url}/api/ble/config")
        with urllib.request.urlopen(req, timeout=5) as resp:
            cfg = json.loads(resp.read().decode())
            print(f"      Service UUID    : {cfg.get('service_uuid')}")
            print(f"      Challenge UUID  : {cfg.get('challenge_uuid')}")
            print(f"      Response UUID   : {cfg.get('response_uuid')}")
            print(f"      Door Status UUID: {cfg.get('door_status_uuid')}")
            print(f"      RSSI Threshold  : {cfg.get('rssi_threshold')} dBm")
    except Exception as e:
        print(f"[-] Failed to fetch config: {e}")
        return False

    # Step 2: Request Nonce Challenge
    mac = "F4:B7:E2:89:12:34"
    print(f"[2/5] Requesting Challenge Nonce from gateway for MAC {mac} ...")
    chal_data = json.dumps({"mac": mac}).encode()
    req = urllib.request.Request(f"{gateway_url}/api/ble/challenge", data=chal_data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as resp:
        chal_resp = json.loads(resp.read().decode())
        nonce = chal_resp.get("nonce")
        print(f"      Challenge Nonce: {nonce} (Freshness window: 5s)")

    # Step 3: Compute Authenticated HMAC Response
    response_token = compute_hmac_sha256(secret_key, nonce)
    print(f"[3/5] Computed HMAC-SHA256 Response: {response_token}")

    # Step 4: Transmit Authenticated Payload to Response / Unlock Char
    payload = {
        "mac": mac,
        "credential_hash": secret_key,
        "response": response_token,
        "rssi": rssi
    }
    print(f"[4/5] Transmitting Authenticated JSON Payload to Unlock Characteristic ...")
    req = urllib.request.Request(f"{gateway_url}/api/ble/authenticate", data=json.dumps(payload).encode(), headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            auth_result = json.loads(resp.read().decode())
            print(f"      Authorized : {auth_result.get('authorized')}")
            print(f"      Door Status: 0x0{auth_result.get('door_status')} (0x01 = UNLATCHED)")
            print(f"      Message    : {auth_result.get('message')}")
            assert auth_result.get("authorized") is True
    except urllib.error.HTTPError as e:
        print(f"[-] Auth failed: {e.read().decode()}")
        return False

    # Step 5: Verify Door Status Characteristic
    print(f"[5/5] Querying Door Status Characteristic (UUID: 9F82A004) ...")
    req = urllib.request.Request(f"{gateway_url}/api/ble/door-status")
    with urllib.request.urlopen(req, timeout=5) as resp:
        door_resp = json.loads(resp.read().decode())
        print(f"      Status: 0x0{door_resp.get('door_status')} [{door_resp.get('status_label')}]")
        assert door_resp.get("door_status") == 1

    print("\n=======================================================")
    print("  [SUCCESS] PATH 2 PHYSICAL GATEWAY HANDSHAKE VERIFIED!")
    print("=======================================================\n")
    return True

async def test_via_bleak(secret_key: str):
    """Executes direct over-the-air BLE GATT test using bleak."""
    try:
        from bleak import BleakScanner, BleakClient
    except ImportError:
        print("[!] The 'bleak' Python library is required for over-the-air BLE testing.")
        print("    Install it via: pip install bleak")
        return False

    print("\n=======================================================")
    print("  SCANNING FOR PHYSICAL SENZR GATEWAY (UUID 9F82A001)...")
    print("=======================================================")

    device = None
    devices = await BleakScanner.discover(timeout=5.0)
    for d in devices:
        uuids = [u.lower() for u in (d.metadata.get("uuids") or [])]
        if SERVICE_UUID in uuids or (d.name and "senzr" in d.name.lower()):
            device = d
            break

    if not device:
        print(f"[-] SENZR Gateway with Service UUID {SERVICE_UUID} not found in range.")
        print("    Make sure BLE advertising is started on the physical gateway:")
        print("    senzr-ctl ble start  OR  systemctl restart edge-gateway")
        return False

    print(f"[+] Found SENZR Gateway: {device.name} ({device.address}), RSSI: {device.rssi} dBm")
    if device.rssi < PROXIMITY_RSSI:
        print(f"[!] Warning: Device RSSI {device.rssi} dBm is below proximity threshold ({PROXIMITY_RSSI} dBm). Move phone/antenna closer (< 3m).")

    async with BleakClient(device) as client:
        print(f"[+] Connected to physical gateway: {client.is_connected}")

        # Read Challenge Nonce
        raw_nonce = await client.read_gatt_char(CHALLENGE_UUID)
        nonce = raw_nonce.decode("utf-8").strip()
        print(f"[+] Received Challenge Nonce: {nonce}")

        # Compute HMAC
        token = compute_hmac_sha256(secret_key, nonce)
        print(f"[+] Computed HMAC Token: {token}")

        # Write Response JSON payload
        payload = json.dumps({"credential_hash": secret_key, "response": token})
        await client.write_gatt_char(RESPONSE_UUID, payload.encode("utf-8"), response=True)
        print(f"[+] Wrote authenticated JSON payload to Unlock Characteristic")

        # Read Door Status Char
        door_status_bytes = await client.read_gatt_char(DOOR_STATUS_UUID)
        door_val = int(door_status_bytes[0]) if len(door_status_bytes) > 0 else 0
        print(f"[+] Read Door Status: 0x0{door_val} ({'UNLATCHED (Door Open)' if door_val == 1 else 'LATCHED'})")
        assert door_val == 1
        print("\n[SUCCESS] Physical door unlatch confirmed over BLE GATT!")
        return True

def main():
    parser = argparse.ArgumentParser(description="SENZR Physical Gateway BLE & REST Test Utility")
    parser.add_argument("--mode", choices=["rest", "ble"], default="rest", help="Test mode: 'rest' for HTTP loopback, 'ble' for real over-the-air Bluetooth")
    parser.add_argument("--url", default="http://127.0.0.1:8080", help="Gateway HTTP base URL")
    parser.add_argument("--secret", default=DEFAULT_SECRET, help="Provisioned secret key")
    parser.add_argument("--rssi", type=int, default=-68, help="Simulated RSSI for REST mode")
    args = parser.parse_args()

    if args.mode == "rest":
        success = test_via_rest(args.url, args.secret, args.rssi)
        sys.exit(0 if success else 1)
    else:
        import asyncio
        success = asyncio.run(test_via_bleak(args.secret))
        sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
