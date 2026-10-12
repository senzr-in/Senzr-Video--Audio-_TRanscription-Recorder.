# SENZR EdgeOS — BLE Hardware Bring-Up Post-Mortem & Troubleshooting Guide
**Device:** Orange Pi 5 Max (Rockchip RK3588, Linux 5.10.160, AMPAK AP6275P BT 5.3)  
**Target:** Production BLE GATT Peripheral (`SENZR`, Service UUID: `9F82A001-7B34-4C12-8921-5092A1B2C3D4`)  
**Document Purpose:** Complete record of all mistakes, failed attempts, root cause diagnostics, and verified fixes to prevent recurring issues across development and production deployments.

---

## Table of Contents
1. [Executive Summary](#1-executive-summary)
2. [Chronological Mistakes & Dead Ends (What Went Wrong & Why)](#2-chronological-mistakes--dead-ends)
   - [Mistake 1: Relying on Legacy `hciconfig leadv 0`](#mistake-1-relying-on-legacy-hciconfig-leadv-0)
   - [Mistake 2: Assuming BlueZ `LEAdvertisingManager1` Is Always Available](#mistake-2-assuming-bluez-leadvertisingmanager1-is-always-available)
   - [Mistake 3: The "Active Passive Scan Lock" Red Herring](#mistake-3-the-active-passive-scan-lock-red-herring)
   - [Mistake 4: Attempting to Modify Active Advertising Sets Without Disabling First](#mistake-4-attempting-to-modify-active-advertising-sets-without-disabling-first)
   - [Mistake 5: HCI Command Parameter Length Mismatch (24 vs 25 Bytes)](#mistake-5-hci-command-parameter-length-mismatch-24-vs-25-bytes)
   - [Mistake 6: Broadcasting Non-Legacy `ADV_EXT_IND` (Invisible to Phones)](#mistake-6-broadcasting-non-legacy-adv_ext_ind-invisible-to-phones)
   - [Mistake 7: Python `AF_BLUETOOTH` Raw Socket Binding Syntax](#mistake-7-python-af_bluetooth-raw-socket-binding-syntax)
   - [Mistake 8: Omitting 128-bit Service UUID from Advertising Payload](#mistake-8-omitting-128-bit-service-uuid-from-advertising-payload)
3. [The Actual Root Causes (3 Architectural Layers)](#3-the-actual-root-causes)
4. [The Working, Verified Sequence (The Gold Standard)](#4-the-working-verified-sequence)
5. [Prevention Checklist for Embedded Linux BLE Bring-Up](#5-prevention-checklist-for-embedded-linux-ble-bring-up)

---

## 1. Executive Summary

During initial testing on the Orange Pi 5 Max, BLE peripheral advertising failed repeatedly. Standard high-level and low-level Linux tools (`bluetoothctl advertise on`, `hciconfig leadv 0`, `btmgmt advertising on`, D-Bus `LEAdvertisingManager1`) all returned errors such as:
- `Status 12 / 0x0C (Command Disallowed)`
- `Status 0x0C (Not Supported)`
- `DBusException: UnknownObject / Method not found`

Through deep tracing with `btmon` and kernel `journalctl`, we discovered a **three-way mismatch** between the Bluetooth 5.3 physical controller, the legacy Rockchip 5.10 kernel, and the BlueZ userspace daemon. 

By applying native Bluetooth 5.x Extended Advertising commands (`0x0035`–`0x0039`) with legacy compatibility mode (`0x0013`), advertising was successfully restored, phones immediately detected the device, and the GATT authentication handshake was completed.

---

## 2. Chronological Mistakes & Dead Ends

### Mistake 1: Relying on Legacy `hciconfig leadv 0`
* **What was attempted:**
  ```bash
  sudo hciconfig hci0 leadv 0
  # or raw HCI equivalent:
  sudo hcitool -i hci0 cmd 0x08 0x0006 A0 00 00 01 00 00 00 00 00 00 00 00 00 07 00
  ```
* **Observed Error:**
  `LE set advertise enable on hci0 returned status 12` (0x0C: Command Disallowed).
* **Why it failed:**
  The AP6275P chip is **Bluetooth 5.3**. Per Bluetooth Core Spec Vol 4, Part E, Section 7.8.9:
  > *"If the Host issues this command when the Controller has Extended Advertising enabled, the Controller shall return the error code Command Disallowed (0x0C)."*
  Controllers that support Bluetooth 5.x Extended Advertising permanently reject Bluetooth 4.0 legacy advertising commands.
* **Lesson Learned:**
  **Never use `hciconfig leadv` or legacy HCI opcodes (`0x0006`, `0x0008`, `0x000a`) on Bluetooth 5.0+ controllers.** Always check controller version via `hciconfig hci0 version` (HCI Version 12 = BT 5.3).

---

### Mistake 2: Assuming BlueZ `LEAdvertisingManager1` Is Always Available
* **What was attempted:**
  Relying on BlueZ's D-Bus interface `org.bluez.LEAdvertisingManager1` (via `bluetoothctl advertise on` or Python D-Bus scripts).
* **Observed Error:**
  `Failed to register advertisement: GDBus.Error:org.freedesktop.DBus.Error.UnknownObject: Method "RegisterAdvertisement" with signature "oa{sv}" on interface "org.bluez.LEAdvertisingManager1" doesn't exist`
* **Why it failed:**
  On system boot, BlueZ (`bluetoothd`) queries the Linux kernel's management socket for advertising capabilities:
  ```text
  src/advertising.c:read_adv_features_callback() Failed to read advertising features: Not Supported (0x0c)
  ```
  The Rockchip Linux 5.10 kernel does **not** implement the management API for Extended Advertising (that kernel feature was only added in Linux 5.17+). Because the kernel returned `Not Supported`, BlueZ aborted registering `LEAdvertisingManager1`.
* **Lesson Learned:**
  **On Linux kernels older than 5.17 with BT 5.0+ hardware, BlueZ D-Bus advertising will fail.** Do not waste time debugging BlueZ D-Bus permissions or configuration files. Use a hardware-level HCI fallback.

---

### Mistake 3: The "Active Passive Scan Lock" Red Herring
* **What was attempted:**
  Assuming BlueZ was holding a passive LE scan open that caused `0x0C Command Disallowed`. We stopped `bluetoothd`, reset the adapter, and attempted to disable scanning:
  ```bash
  sudo systemctl stop bluetooth
  sudo hciconfig hci0 reset
  sudo hcitool -i hci0 cmd 0x08 0x000c 00 00
  ```
* **Why it was a red herring:**
  Even with BlueZ completely stopped, the legacy command `0x08 0x0006` still failed with `0x0C`. The rejection was coming directly from the controller's link layer firmware because the controller is a BT 5.3 chip, not because of an active scan lock.
* **Lesson Learned:**
  Distinguish between host-level conflicts and controller firmware restrictions. If a command fails immediately after a hardware reset (`hciconfig reset`), it is a specification/command version incompatibility, not a daemon state conflict.

---

### Mistake 4: Attempting to Modify Active Advertising Sets Without Disabling First
* **What was attempted:**
  Attempting to re-run the parameter configuration command `0x0036` or enable command `0x0039` after an earlier run:
  ```bash
  sudo hcitool -i hci0 cmd 0x08 0x0036 01 01 00 ...
  sudo hcitool -i hci0 cmd 0x08 0x0039 01 01 01 00 00 00
  ```
* **Observed Error:**
  `> HCI Event: 0x0e plen 4: 01 36 20 0C` (Status 0x0C: Command Disallowed).
* **Why it failed:**
  Under the Bluetooth 5.x Extended Advertising specification, **an advertising handle cannot have its parameters changed while it is currently enabled**. Re-enabling an already enabled set also returns `0x0C`.
* **Lesson Learned:**
  **Always issue a disable command (`0x0039 00 00`) before reconfiguring advertising parameters or data.**

---

### Mistake 5: HCI Command Parameter Length Mismatch (24 vs 25 Bytes)
* **What was attempted:**
  Sending 24 bytes for `HCI_LE_Set_Extended_Advertising_Parameters`:
  ```bash
  # INCORRECT (24 bytes - missing filter policy):
  sudo hcitool -i hci0 cmd 0x08 0x0036 01 01 00 A0 00 00 00 01 00 07 01 00 00 00 00 00 00 00 7F 01 00 01 01 00
  ```
* **Observed Error:**
  `> HCI Event: 0x0e plen 4: 01 36 20 12` (Status 0x12: Invalid HCI Command Parameters).
* **Why it failed:**
  Per BT Core Spec Section 7.8.53, the command takes exactly 25 bytes. The octet for `Advertising_Filter_Policy` (`00`) was omitted before `Advertising_Tx_Power` (`7F`).
* **Lesson Learned:**
  Carefully verify exact struct byte offsets when issuing raw HCI commands:
  | Field | Size | Correct Value |
  |---|---|---|
  | Advertising Handle | 1 byte | `01` |
  | Advertising Event Properties | 2 bytes | `13 00` |
  | Primary Adv Interval Min | 3 bytes | `A0 00 00` (100ms) |
  | Primary Adv Interval Max | 3 bytes | `00 01 00` (160ms) |
  | Primary Adv Channel Map | 1 byte | `07` (Channels 37, 38, 39) |
  | Own Address Type | 1 byte | `01` (Random) |
  | Peer Address Type | 1 byte | `00` (Public) |
  | Peer Address | 6 bytes | `00 00 00 00 00 00` |
  | Advertising Filter Policy | 1 byte | **`00`** *(was missing)* |
  | Advertising Tx Power | 1 byte | `7F` (No preference) |
  | Primary Adv PHY | 1 byte | `01` (LE 1M) |
  | Secondary Adv Max Skip | 1 byte | `00` |
  | Secondary Adv PHY | 1 byte | `01` (LE 1M) |
  | Advertising SID | 1 byte | `01` |
  | Scan Request Notification Enable | 1 byte | `00` |
  | **Total** | **25 bytes** | |

---

### Mistake 6: Broadcasting Non-Legacy `ADV_EXT_IND` (Invisible to Phones)
* **What was attempted:**
  Setting `Advertising_Event_Properties` to `01 00` (0x0001: Connectable only).
* **Why it failed:**
  - `0x0001` instructs the controller to transmit modern Bluetooth 5.0 `ADV_EXT_IND` packets.
  - Standard smartphones (iOS and Android) running stock BLE scans **only scan primary advertising channels (37, 38, 39) for legacy `ADV_IND` PDUs**. They do not scan for extended auxiliary pointer packets unless an app explicitly enables extended scanning.
  - The controller was advertising, but no phone could see it!
* **The Solution:**
  Use property mask **`0x0013`** (`13 00` in little-endian):
  - Bit 0 (`0x0001`): Connectable
  - Bit 1 (`0x0002`): Scannable
  - Bit 4 (`0x0010`): **Use legacy advertising PDUs (`ADV_IND`)**
* **Lesson Learned:**
  **Always set bit 4 (`0x0013`) when using Extended Advertising commands if smartphones need to discover the device.**

---

### Mistake 7: Python `AF_BLUETOOTH` Raw Socket Binding Syntax
* **What was attempted:**
  ```python
  s = socket.socket(31, socket.SOCK_RAW, 1) # AF_BLUETOOTH, BTPROTO_HCI
  s.bind(struct.pack('HHH', 31, 0xFFFF, 3))
  ```
* **Observed Error:**
  `OSError: bind(): wrong format`
* **Why it failed:**
  Python's C wrapper for Linux `sockaddr_hci` parses socket bind arguments as a Python **tuple of integers** `(dev, channel)`:
  ```python
  s.bind((0xFFFF, 3)) # (HCI_DEV_NONE=0xFFFF, HCI_CHANNEL_CONTROL=3)
  ```
  Passing a packed `bytes` object causes `PyArg_ParseTuple` to fail.
* **Lesson Learned:**
  In Python on Linux, Bluetooth HCI sockets take a tuple `(device_id, channel_id)`.

---

### Mistake 8: Omitting 128-bit Service UUID from Advertising Payload
* **What was attempted:**
  Only including Flags (`02 01 06`) and Name `SENZR` (`06 09 53 45 4E 5A 52`) in the payload (10 bytes).
* **Why it causes mobile problems:**
  Many production mobile apps (including Flutter with `flutter_blue_plus` and native iOS `CBCentralManager`) scan with:
  ```dart
  FlutterBluePlus.startScan(withServices: [Guid("9F82A001-7B34-4C12-8921-5092A1B2C3D4")]);
  ```
  On iOS, iOS CoreBluetooth **completely suppresses and hides** advertising packets that do not include the filtered Service UUID in the advertisement payload!
* **The Solution:**
  Fit **both** Name and Service UUID into the 31-byte legacy limit:
  - Flags (3 bytes): `02 01 06`
  - Name "SENZR" (7 bytes): `06 09 53 45 4E 5A 52`
  - 128-bit Service UUID (18 bytes): `11 07 D4 C3 B2 A1 92 50 21 89 12 4C 34 7B 01 A0 82 9F`
  - **Total:** $3 + 7 + 18 = 28\text{ bytes} \le 31\text{ bytes}$ limit (`0x1C` length).

---

## 3. The Actual Root Causes

| Layer | Component | Behavior / Constraint |
|---|---|---|
| **Hardware** | AMPAK AP6275P (BT 5.3) | Native Extended Advertising controller. Forbids Bluetooth 4.0 legacy commands (`0x0006`). Throws `Status 12 (Command Disallowed)`. |
| **Kernel** | Rockchip Linux 5.10.160 | Lacks Extended Advertising support in `net/bluetooth/mgmt.c` (only introduced in Linux 5.17+). Returns `0x0C Not Supported` on `MGMT_OP_READ_ADV_FEATURES`. |
| **BlueZ** | `bluetoothd` (v5.64) | Fails to read advertising features from kernel 5.10. Aborts registering D-Bus interface `org.bluez.LEAdvertisingManager1`. |
| **Link Layer** | Mobile compatibility | Phones only listen for `ADV_IND` PDUs on channels 37/38/39 by default. Requires properties mask `0x0013`. |

---

## 4. The Working, Verified Sequence

Copy-paste this exact sequence to start BLE advertising on Orange Pi 5 Max:

```bash
# 1. Disable all active advertising sets first
sudo hcitool -i hci0 cmd 0x08 0x0039 00 00

# 2. Set Random Address for handle 0x01 (MAC in little-endian format)
sudo hcitool -i hci0 cmd 0x08 0x0035 01 F2 F5 ED 35 F5 C0

# 3. Set Advertising Parameters (handle 0x01, properties=0x0013 ADV_IND legacy connectable)
sudo hcitool -i hci0 cmd 0x08 0x0036 01 13 00 A0 00 00 00 01 00 07 01 00 00 00 00 00 00 00 00 7F 01 00 01 01 00

# 4. Set Advertising Data (Flags + Name "SENZR" + 128-bit Service UUID 9F82A001) = 28 bytes total
sudo hcitool -i hci0 cmd 0x08 0x0037 01 03 01 1C 02 01 06 06 09 53 45 4E 5A 52 11 07 D4 C3 B2 A1 92 50 21 89 12 4C 34 7B 01 A0 82 9F

# 5. Enable Advertising on handle 0x01
sudo hcitool -i hci0 cmd 0x08 0x0039 01 01 01 00 00 00
```

---

## 5. Prevention Checklist for Embedded Linux BLE Bring-Up

Before debugging BLE advertising issues on any new board (Orange Pi, Raspberry Pi, Jetson, Radxa):

1. **Check Controller Version First:**
   ```bash
   hciconfig hci0 version
   ```
   - If `HCI Version >= 0x9` (BT 5.0+): **Do not use `hciconfig leadv`**. Use Extended Advertising commands.
2. **Check Kernel Version:**
   ```bash
   uname -r
   ```
   - If kernel `< 5.17` and controller is BT 5.0+: Expect BlueZ `LEAdvertisingManager1` to be missing. Use hardware-level Extended Advertising commands.
3. **Always Check Parameters:**
   - Property mask must be `0x0013` for legacy smartphone scanner discovery.
   - Total payload length must be $\le 31$ bytes.
   - Always include the 128-bit Primary Service UUID in the payload for iOS CoreBluetooth discovery.
4. **Always Reset Handles Before Configuration:**
   - Always run `hcitool cmd 0x08 0x0039 00 00` before writing new parameters or payload.
