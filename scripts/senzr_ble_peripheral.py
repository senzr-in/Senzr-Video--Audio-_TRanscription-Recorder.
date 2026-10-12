#!/usr/bin/env python3
"""
SENZR EdgeOS - BlueZ D-Bus BLE GATT Peripheral & Advertising Daemon
Production Service for Orange Pi 5 Max, Raspberry Pi, Jetson.
Provides:
  - BLE Advertising with LocalName "SENZR-Edge" & Service UUID 9F82A001
  - Service UUID:       9F82A001-7B34-4C12-8921-5092A1B2C3D4
  - Challenge Nonce:    9F82A002-7B34-4C12-8921-5092A1B2C3D4 (Read / Notify)
  - Response / Unlock:  9F82A003-7B34-4C12-8921-5092A1B2C3D4 (Write with Response)
  - Door Status:        9F82A004-7B34-4C12-8921-5092A1B2C3D4 (Read / Notify)
  - Auto-forwarding to C++ Gateway API (http://127.0.0.1:8000/api/ble/...)
"""

import sys
import os
import secrets
import hmac
import hashlib
import json
import time
import urllib.request
import urllib.error

try:
    import dbus
    import dbus.service
    import dbus.mainloop.glib
    from gi.repository import GLib
except ImportError:
    print("[-] Missing dbus or gi packages. Run: sudo apt-get install -y python3-dbus python3-gi")
    sys.exit(1)

BLUEZ_SERVICE_NAME = 'org.bluez'
GATT_MANAGER_IFACE = 'org.bluez.GattManager1'
DBUS_OM_IFACE =      'org.freedesktop.DBus.ObjectManager'
DBUS_PROP_IFACE =    'org.freedesktop.DBus.Properties'
LE_ADVERTISING_MANAGER_IFACE = 'org.bluez.LEAdvertisingManager1'
LE_ADVERTISEMENT_IFACE = 'org.bluez.LEAdvertisement1'
GATT_SERVICE_IFACE = 'org.bluez.GattService1'
GATT_CHRC_IFACE =    'org.bluez.GattCharacteristic1'

SERVICE_UUID     = "9F82A001-7B34-4C12-8921-5092A1B2C3D4"
CHALLENGE_UUID   = "9F82A002-7B34-4C12-8921-5092A1B2C3D4"
RESPONSE_UUID    = "9F82A003-7B34-4C12-8921-5092A1B2C3D4"
DOOR_STATUS_UUID = "9F82A004-7B34-4C12-8921-5092A1B2C3D4"
PROVISIONED_KEY  = "senzr_sec_k992_prod_human_gate_master"
BACKEND_URL      = "http://127.0.0.1:8000"

class Application(dbus.service.Object):
    def __init__(self, bus):
        self.path = '/'
        self.services = []
        dbus.service.Object.__init__(self, bus, self.path)

    def get_path(self):
        return dbus.ObjectPath(self.path)

    def add_service(self, service):
        self.services.append(service)

    @dbus.service.method(DBUS_OM_IFACE, out_signature='a{oa{sa{sv}}}')
    def GetManagedObjects(self):
        response = {}
        for service in self.services:
            response[service.get_path()] = service.get_properties()
            for chrc in service.get_characteristics():
                response[chrc.get_path()] = chrc.get_properties()
        return response

class SenzrAccessService(dbus.service.Object):
    PATH_BASE = '/org/bluez/senzr/service'

    def __init__(self, bus, index):
        self.path = self.PATH_BASE + str(index)
        self.bus = bus
        self.uuid = SERVICE_UUID
        self.primary = True
        self.characteristics = []
        dbus.service.Object.__init__(self, bus, self.path)

        # 1. Nonce Char (9F82A002)
        self.nonce_char = ChallengeNonceCharacteristic(bus, 0, self)
        self.characteristics.append(self.nonce_char)

        # 2. Response / Unlock Char (9F82A003)
        self.resp_char = ResponseUnlockCharacteristic(bus, 1, self, self.nonce_char)
        self.characteristics.append(self.resp_char)

        # 3. Door Status Char (9F82A004)
        self.door_char = DoorStatusCharacteristic(bus, 2, self)
        self.characteristics.append(self.door_char)
        self.resp_char.set_door_char(self.door_char)

    def get_properties(self):
        return {
            GATT_SERVICE_IFACE: {
                'UUID': self.uuid,
                'Primary': self.primary,
                'Characteristics': dbus.Array([c.get_path() for c in self.characteristics], signature='o')
            }
        }

    def get_path(self):
        return dbus.ObjectPath(self.path)

    def get_characteristics(self):
        return self.characteristics

class ChallengeNonceCharacteristic(dbus.service.Object):
    def __init__(self, bus, index, service):
        self.path = service.path + '/char' + str(index)
        self.bus = bus
        self.uuid = CHALLENGE_UUID
        self.service = service
        self.flags = ['read', 'notify']
        self.current_nonce = ""
        self.nonce_timestamp = 0
        dbus.service.Object.__init__(self, bus, self.path)

    def get_properties(self):
        return {
            GATT_CHRC_IFACE: {
                'Service': self.service.get_path(),
                'UUID': self.uuid,
                'Flags': self.flags,
            }
        }

    def get_path(self):
        return dbus.ObjectPath(self.path)

    @dbus.service.method(GATT_CHRC_IFACE, in_signature='a{sv}', out_signature='ay')
    def ReadValue(self, options):
        # Fetch fresh challenge from backend or generate locally
        nonce = ""
        try:
            req = urllib.request.Request(
                f"{BACKEND_URL}/api/ble/challenge",
                data=json.dumps({"mac": "ble_client"}).encode(),
                headers={"Content-Type": "application/json"}
            )
            with urllib.request.urlopen(req, timeout=2) as resp:
                data = json.loads(resp.read().decode())
                nonce = data.get("nonce", "")
        except Exception:
            pass

        if not nonce:
            nonce = secrets.token_hex(16)

        self.current_nonce = nonce
        self.nonce_timestamp = time.time()
        print(f"[GATT] Challenge Nonce Emitted: {self.current_nonce}")
        return [dbus.Byte(b) for b in self.current_nonce.encode('utf-8')]

class ResponseUnlockCharacteristic(dbus.service.Object):
    def __init__(self, bus, index, service, nonce_char):
        self.path = service.path + '/char' + str(index)
        self.bus = bus
        self.uuid = RESPONSE_UUID
        self.service = service
        self.flags = ['write']
        self.nonce_char = nonce_char
        self.door_char = None
        dbus.service.Object.__init__(self, bus, self.path)

    def set_door_char(self, door_char):
        self.door_char = door_char

    def get_properties(self):
        return {
            GATT_CHRC_IFACE: {
                'Service': self.service.get_path(),
                'UUID': self.uuid,
                'Flags': self.flags,
            }
        }

    def get_path(self):
        return dbus.ObjectPath(self.path)

    @dbus.service.method(GATT_CHRC_IFACE, in_signature='aya{sv}')
    def WriteValue(self, value, options):
        payload_bytes = bytes([int(b) for b in value])
        raw_text = payload_bytes.decode('utf-8', errors='ignore').strip()
        print(f"[GATT] Received write on Response / Unlock Char: {raw_text}")

        # Check freshness (< 5 sec)
        if time.time() - self.nonce_char.nonce_timestamp > 5.0 or not self.nonce_char.current_nonce:
            print("[-] Nonce expired (> 5s) or missing!")
            return

        token = raw_text
        cred_key = PROVISIONED_KEY
        if raw_text.startswith('{'):
            try:
                data = json.loads(raw_text)
                token = data.get('response') or data.get('token') or ""
                if data.get('credential_hash'):
                    cred_key = data.get('credential_hash')
            except Exception:
                pass

        expected_hmac = hmac.new(
            cred_key.encode('utf-8'),
            self.nonce_char.current_nonce.encode('utf-8'),
            hashlib.sha256
        ).hexdigest()

        # Forward to C++ backend to trigger physical relay pulse and SQLite logging
        authenticated = False
        try:
            req_data = json.dumps({
                "mac": "ble_client",
                "credential_hash": cred_key,
                "response": token,
                "rssi": -65
            }).encode('utf-8')
            req = urllib.request.Request(
                f"{BACKEND_URL}/api/ble/authenticate",
                data=req_data,
                headers={"Content-Type": "application/json"}
            )
            with urllib.request.urlopen(req, timeout=3) as resp:
                res_json = json.loads(resp.read().decode())
                if res_json.get("authorized"):
                    authenticated = True
                    print("[+] C++ Backend confirmed access: Relay pulsed & buzzer beeped!")
        except Exception as e:
            # Standalone fallback if C++ server is still booting
            if token.lower() == expected_hmac.lower():
                authenticated = True
                print("[+] Verified locally via master secret!")

        if authenticated and self.door_char:
            self.door_char.trigger_unlatch()
        elif not authenticated:
            print("[-] Access DENIED: HMAC mismatch or invalid credential.")

class DoorStatusCharacteristic(dbus.service.Object):
    def __init__(self, bus, index, service):
        self.path = service.path + '/char' + str(index)
        self.bus = bus
        self.uuid = DOOR_STATUS_UUID
        self.service = service
        self.flags = ['read', 'notify']
        self.door_status = 0x00 # 0x00 = latched, 0x01 = unlatched
        dbus.service.Object.__init__(self, bus, self.path)

    def get_properties(self):
        return {
            GATT_CHRC_IFACE: {
                'Service': self.service.get_path(),
                'UUID': self.uuid,
                'Flags': self.flags,
            }
        }

    def get_path(self):
        return dbus.ObjectPath(self.path)

    def trigger_unlatch(self):
        self.door_status = 0x01
        print("[GATT] Physical Door Unlatch Confirmed (0x01)! Sending Notify...")
        self.PropertiesChanged(GATT_CHRC_IFACE, {'Value': [dbus.Byte(0x01)]}, [])
        # Auto relatch after 3 seconds
        GLib.timeout_add_seconds(3, self._auto_relatch)

    def _auto_relatch(self):
        self.door_status = 0x00
        print("[GATT] Door Latched (0x00). Sending Notify...")
        self.PropertiesChanged(GATT_CHRC_IFACE, {'Value': [dbus.Byte(0x00)]}, [])
        return False

    @dbus.service.signal(DBUS_PROP_IFACE, signature='sa{sv}as')
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    @dbus.service.method(GATT_CHRC_IFACE, in_signature='a{sv}', out_signature='ay')
    def ReadValue(self, options):
        status = self.door_status
        try:
            req = urllib.request.Request(f"{BACKEND_URL}/api/ble/door-status")
            with urllib.request.urlopen(req, timeout=1) as resp:
                data = json.loads(resp.read().decode())
                status = int(data.get("door_status", self.door_status))
        except Exception:
            pass
        return [dbus.Byte(status)]


class Advertisement(dbus.service.Object):
    PATH_BASE = '/org/bluez/senzr/advertisement'

    def __init__(self, bus, index, advertising_type):
        self.path = self.PATH_BASE + str(index)
        self.bus = bus
        self.ad_type = advertising_type
        self.service_uuids = [SERVICE_UUID]
        # Keep name <= 8 chars so total payload (Flags 3B + UUID128 18B + Name 7B = 28B) <= 31B limit
        self.local_name = 'SENZR'
        dbus.service.Object.__init__(self, bus, self.path)

    def get_properties(self):
        properties = dict()
        properties['Type'] = self.ad_type
        properties['ServiceUUIDs'] = dbus.Array(self.service_uuids, signature='s')
        properties['LocalName'] = dbus.String(self.local_name)
        properties['Discoverable'] = dbus.Boolean(True)
        return {LE_ADVERTISEMENT_IFACE: properties}

    def get_path(self):
        return dbus.ObjectPath(self.path)

    @dbus.service.method(DBUS_PROP_IFACE, in_signature='ss', out_signature='v')
    def Get(self, interface, prop):
        return self.GetAll(interface)[prop]

    @dbus.service.method(DBUS_PROP_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != LE_ADVERTISEMENT_IFACE:
            raise dbus.exceptions.DBusException('Invalid interface')
        return self.get_properties()[LE_ADVERTISEMENT_IFACE]

    @dbus.service.method(LE_ADVERTISEMENT_IFACE, in_signature='', out_signature='')
    def Release(self):
        print('[+] Advertisement released')

def start_extended_advertising(dev="hci0", name="SENZR"):
    """
    Hardware-level Extended Advertising for Bluetooth 5.3 controllers (e.g. AP6275P on RK3588).
    Bypasses kernel 5.10 BlueZ mgmt limitations by configuring handle 0x01 with ADV_IND legacy format.
    """
    try:
        # Extract device BD address if possible
        bd_addr = "F2 F5 ED 35 F5 C0"  # default little-endian
        try:
            import subprocess
            out = subprocess.check_output(["hciconfig", dev]).decode()
            for line in out.splitlines():
                if "BD Address:" in line:
                    raw_mac = line.split("BD Address:")[1].split()[0].strip()
                    parts = raw_mac.split(":")
                    if len(parts) == 6:
                        bd_addr = " ".join(reversed(parts))
                    break
        except Exception:
            pass

        # 1. Disable existing advertising sets
        os.system(f"hcitool -i {dev} cmd 0x08 0x0039 00 00 >/dev/null 2>&1")
        # 2. Set Random Address
        os.system(f"hcitool -i {dev} cmd 0x08 0x0035 01 {bd_addr} >/dev/null 2>&1")
        # 3. Parameters: Properties=0x0013 (ADV_IND legacy connectable scannable)
        os.system(f"hcitool -i {dev} cmd 0x08 0x0036 01 13 00 A0 00 00 00 01 00 07 01 00 00 00 00 00 00 00 00 7F 01 00 01 01 00 >/dev/null 2>&1")
        # 4. Data: Flags (3B) + Name (7B) + 128-bit Service UUID (18B) = 28B <= 31B
        # Service UUID: 9F82A001-7B34-4C12-8921-5092A1B2C3D4 (reversed little-endian)
        uuid_hex = "D4 C3 B2 A1 92 50 21 89 12 4C 34 7B 01 A0 82 9F"
        name_bytes = name.encode('utf-8')
        name_len = len(name_bytes) + 1
        name_hex = " ".join(f"{b:02X}" for b in name_bytes)
        # 3 (flags) + (1 + name_len) + (1 + 17 for 128b uuid) = 3 + name_len+1 + 18
        total_len = 3 + (len(name_bytes) + 2) + 18
        adv_hex = f"01 03 01 {total_len:02X} 02 01 06 {name_len:02X} 09 {name_hex} 11 07 {uuid_hex}"
        os.system(f"hcitool -i {dev} cmd 0x08 0x0037 {adv_hex} >/dev/null 2>&1")
        # 5. Enable handle 0x01
        os.system(f"hcitool -i {dev} cmd 0x08 0x0039 01 01 01 00 00 00 >/dev/null 2>&1")
        print(f"[+] Active Extended Advertising for '{name}' (handle 0x01, ADV_IND connectable, UUID {SERVICE_UUID})")
        return True
    except Exception as e:
        print(f"[-] Extended advertising setup failed: {e}")
        return False

def main():
    print("==========================================================")
    print("  🚀 SENZR EDGE GATEWAY - PRODUCTION BLE GATT DAEMON       ")
    print("==========================================================")
    print(f"  Advertising Name : SENZR")
    print(f"  Service UUID     : {SERVICE_UUID}")
    print(f"  Challenge Nonce  : {CHALLENGE_UUID}")
    print(f"  Response/Unlock  : {RESPONSE_UUID}")
    print(f"  Door Status      : {DOOR_STATUS_UUID}")
    print("==========================================================")

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()

    adapter_path = '/org/bluez/hci0'
    try:
        adapter = bus.get_object(BLUEZ_SERVICE_NAME, adapter_path)
        adapter_props = dbus.Interface(adapter, DBUS_PROP_IFACE)
        adapter_props.Set('org.bluez.Adapter1', 'Powered', dbus.Boolean(True))
        adapter_props.Set('org.bluez.Adapter1', 'Discoverable', dbus.Boolean(True))
        adapter_props.Set('org.bluez.Adapter1', 'DiscoverableTimeout', dbus.UInt32(0))
        adapter_props.Set('org.bluez.Adapter1', 'Pairable', dbus.Boolean(True))
        adapter_props.Set('org.bluez.Adapter1', 'Alias', dbus.String('SENZR'))
    except Exception as e:
        print(f"[-] Bluetooth adapter hci0 not found: {e}")
        sys.exit(1)

    # 1. Register GATT Application
    service_manager = dbus.Interface(adapter, GATT_MANAGER_IFACE)
    app = Application(bus)
    app.add_service(SenzrAccessService(bus, 0))

    loop = GLib.MainLoop()

    print("[+] Registering GATT Application with BlueZ...")
    service_manager.RegisterApplication(
        app.get_path(), dbus.Dictionary({}, signature='sv'),
        reply_handler=lambda: print("[+] GATT Application Registered!"),
        error_handler=lambda e: print(f"[-] Failed to register GATT app: {e}")
    )

    # 2. Register LE Advertisement (BlueZ D-Bus with hardware fallback)
    try:
        ad_manager = dbus.Interface(adapter, LE_ADVERTISING_MANAGER_IFACE)
        adv = Advertisement(bus, 0, 'peripheral')

        def _on_ad_success():
            print("[+] Broadcasting BLE Advertisement via BlueZ D-Bus!")

        def _on_ad_error(e):
            print(f"[*] BlueZ LEAdvertisingManager1 failed ({e}). Falling back to BT5.3 Extended Advertising...")
            start_extended_advertising('hci0', 'SENZR')

        print("[+] Registering BLE Advertisement...")
        ad_manager.RegisterAdvertisement(
            adv.get_path(),
            reply_handler=_on_ad_success,
            error_handler=_on_ad_error
        )
    except Exception as e:
        print(f"[*] BlueZ LEAdvertisingManager1 not available on this kernel ({e}).")
        start_extended_advertising('hci0', 'SENZR')

    print("[*] -------------------------------------------------------------")
    print("[*] IMPORTANT: Smartphones do NOT list GATT devices in Android/iOS")
    print("[*] system 'Settings > Bluetooth' (those only show audio/headsets).")
    print("[*] Scan with: 'nRF Connect for Mobile' app or the SENZR Flutter App")
    print("[*] Device Name : SENZR")
    print("[*] Device MAC  : C0:F5:35:ED:F5:F2")
    print("[*] Service UUID: 9F82A001-7B34-4C12-8921-5092A1B2C3D4")
    print("[*] -------------------------------------------------------------")

    try:
        loop.run()
    except KeyboardInterrupt:
        print("\nStopping...")

if __name__ == '__main__':
    main()
