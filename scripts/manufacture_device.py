#!/usr/bin/env python3

import json
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import serial
import serial.tools.list_ports

PROJECT_DIR = Path(__file__).resolve().parents[1]
BURNIN_PROJECT_DIR = PROJECT_DIR / "burn_in_firmware"
BURNIN_ENV = "vintlabs-devkit-v1"
BUILD_SCRIPT = PROJECT_DIR / "scripts" / "build_device.py"
DEVICES_FILE = PROJECT_DIR / "scripts" / "devices.json"
PIO_EXE = Path(r"C:\Users\mdroo\.platformio\penv\Scripts\platformio.exe")
REPORTS_DIR = PROJECT_DIR / "manufacturing_reports"
LABELS_DIR = PROJECT_DIR / "labels"
CLAIM_INVENTORY_FILE = PROJECT_DIR / "claim_inventory" / "claim_inventory.json"
SERVICE_ACCOUNT_FILE = PROJECT_DIR / "secrets" / "firebase-service-account.json"
DATABASE_URL = "https://aiesdoser-default-rtdb.firebaseio.com"
BAUD = 115200
SETUP_URL = "https://aiesdoser.web.app/setup"


def fail(msg):
    print(f"\nERROR: {msg}")
    sys.exit(1)


def now_iso():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def ask_yes_no(prompt, default=False):
    suffix = " [Y/n]: " if default else " [y/N]: "
    while True:
        answer = input(prompt + suffix).strip().lower()
        if not answer:
            return default
        if answer in ("y", "yes"):
            return True
        if answer in ("n", "no"):
            return False


def choose_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        fail("No serial ports found.")
    if len(ports) == 1:
        return ports[0].device
    print("\nAvailable ports:")
    for i, port in enumerate(ports, 1):
        print(f"  {i}. {port.device}  {port.description}")
    while True:
        try:
            choice = int(input("Select port: ").strip())
            if 1 <= choice <= len(ports):
                return ports[choice - 1].device
        except ValueError:
            pass


def run(cmd, cwd, description, input_text=None):
    print(f"\n{description}")
    print(" ".join(map(str, cmd)))
    result = subprocess.run(cmd, cwd=cwd, input=input_text, text=True)
    if result.returncode != 0:
        fail(f"{description} failed.")
    return result


def upload_burnin(port):
    if not (BURNIN_PROJECT_DIR / "platformio.ini").exists():
        fail(f"Burn-in PlatformIO project missing:\n{BURNIN_PROJECT_DIR}")
    run([
        str(PIO_EXE), "run", "--project-dir", str(BURNIN_PROJECT_DIR),
        "-e", BURNIN_ENV, "-t", "upload", "--upload-port", port,
    ], PROJECT_DIR, "Building and uploading universal burn-in firmware...")


def read_identity(port, timeout=30):
    try:
        ser = serial.Serial(port, BAUD, timeout=1)
    except Exception as exc:
        fail(f"Could not open {port}: {exc}")
    time.sleep(2.5)
    ser.reset_input_buffer()
    ser.write(b"IDENTITY\n")
    ser.flush()
    mac = chip = None
    board = "vintlabs-devkit-v1"
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = ser.readline().decode(errors="ignore").strip()
        if not line:
            continue
        print(line)
        m = re.search(r"MAC Address:\s*([0-9A-Fa-f:]+)", line)
        if m:
            mac = m.group(1).upper()
        m = re.search(r"Chip ID:\s*([0-9A-Fa-f]+)", line)
        if m:
            chip = m.group(1).upper()
        m = re.search(r"Board:\s*(.+)", line)
        if m:
            board = m.group(1).strip()
        if mac and chip:
            return ser, {"macAddress": mac, "chipId": chip, "board": board}
    ser.close()
    fail("Burn-in firmware did not report identity.")


def load_devices():
    return json.loads(DEVICES_FILE.read_text(encoding="utf-8")) if DEVICES_FILE.exists() else {}


def save_devices(data):
    if DEVICES_FILE.exists():
        DEVICES_FILE.with_suffix(".json.bak").write_bytes(DEVICES_FILE.read_bytes())
    DEVICES_FILE.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")


def prompt(label, default):
    return input(f"{label} [{default}]: ").strip() or default


def enroll(identity):
    device_id = prompt("Device ID", "reefDoser4")
    customer = prompt("Customer name", "Burn-In Pending")
    label = prompt("Label", "AIES-00004")
    version = prompt("Firmware version", "T-1.0.0")
    db = load_devices()
    if device_id in db and not ask_yes_no(f"{device_id} exists. Overwrite?", False):
        fail("Cancelled.")
    db[device_id] = {
        "customer": customer,
        "label": label,
        "deviceId": device_id,
        "firmwareVersion": version,
        "chipId": identity["chipId"],
        "macAddress": identity["macAddress"],
        "board": identity["board"],
        "otaJsonName": f"{device_id}.json",
        "otaBinName": f"{device_id}.bin",
        "firebasePublic": "C:/Users/mdroo/OneDrive/Firebase/aiesdoser/public",
        "burnInStatus": "RUNNING",
        "burnInStartedAt": datetime.now().isoformat(timespec="seconds"),
    }
    save_devices(db)
    return db[device_id]


def run_burnin(ser, device):
    duration = int(prompt("Burn-in duration minutes", "5"))
    for command in (f"SET_DEVICE_ID {device['deviceId']}", f"SET_DURATION_MIN {duration}", "START"):
        ser.write((command + "\n").encode())
        ser.flush()
        time.sleep(0.2)
    print("\nBurn-in running. Ctrl+C stops safely.")
    lines = []
    result = "FAIL"
    try:
        while True:
            line = ser.readline().decode(errors="ignore").strip()
            if not line:
                continue
            print(line)
            lines.append(line)
            if line.startswith("BURNIN_COMPLETE") and "result=PASS" in line:
                result = "PASS"
                break
    finally:
        ser.close()
    db = load_devices()
    db[device["deviceId"]]["burnInStatus"] = result
    db[device["deviceId"]]["burnInCompletedAt"] = datetime.now().isoformat(timespec="seconds")
    save_devices(db)
    if result != "PASS":
        fail("Burn-in did not pass.")
    REPORTS_DIR.mkdir(exist_ok=True)
    report = REPORTS_DIR / f"{device['deviceId']}_burnin_{datetime.now():%Y%m%d_%H%M%S}.txt"
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")


def load_claim_inventory():
    if not CLAIM_INVENTORY_FILE.exists():
        fail("Claim inventory missing. Run: python scripts\\generate_claim_batch.py")
    return json.loads(CLAIM_INVENTORY_FILE.read_text(encoding="utf-8"))


def save_claim_inventory(inventory):
    CLAIM_INVENTORY_FILE.with_suffix(".json.bak").write_bytes(CLAIM_INVENTORY_FILE.read_bytes())
    inventory["updatedAt"] = now_iso()
    CLAIM_INVENTORY_FILE.write_text(json.dumps(inventory, indent=4) + "\n", encoding="utf-8")


def reserve_next_claim(device):
    inventory = load_claim_inventory()
    claims = inventory.get("claims", [])
    for item in claims:
        if item.get("status") == "ASSIGNED" and item.get("deviceId") == device["deviceId"]:
            if str(item.get("macAddress", "")).upper() != device["macAddress"].upper() or str(item.get("chipId", "")).upper() != device["chipId"].upper():
                fail(f"Existing claim assignment identity mismatch for {device['deviceId']}.")
            return item["claimCode"], item
    unused = sorted(
        [item for item in claims if item.get("status") == "UNUSED"],
        key=lambda item: (str(item.get("batch", "")), int(item.get("sequence", 0))),
    )
    if not unused:
        fail("No unused claim codes remain. Generate another batch.")
    item = unused[0]
    item.update({
        "status": "ASSIGNED",
        "assignedAt": now_iso(),
        "deviceId": device["deviceId"],
        "label": device["label"],
        "macAddress": device["macAddress"].upper(),
        "chipId": device["chipId"].upper(),
        "firebaseStatus": "PENDING",
    })
    save_claim_inventory(inventory)
    return item["claimCode"], item


def set_claim_firebase_status(claim_code, status):
    inventory = load_claim_inventory()
    for item in inventory.get("claims", []):
        if item.get("claimCode") == claim_code:
            item["firebaseStatus"] = status
            item["firebaseUpdatedAt"] = now_iso()
            save_claim_inventory(inventory)
            return


def initialize_firebase():
    try:
        import firebase_admin
        from firebase_admin import credentials, db
    except ImportError:
        fail(f'firebase-admin is not installed. Run:\n"{sys.executable}" -m pip install firebase-admin')
    if not SERVICE_ACCOUNT_FILE.exists():
        fail(f"Firebase service-account file not found:\n{SERVICE_ACCOUNT_FILE}")
    if not firebase_admin._apps:
        firebase_admin.initialize_app(
            credentials.Certificate(str(SERVICE_ACCOUNT_FILE)),
            {"databaseURL": DATABASE_URL},
        )
    return db


def bind_claim_in_firebase(device, claim_code, item):
    firebase_db = initialize_firebase()
    claim_ref = firebase_db.reference(f"/deviceClaims/{device['deviceId']}")
    existing = claim_ref.get()
    if existing and (existing.get("claimed") is True or existing.get("claimedBy")):
        fail(f"{device['deviceId']} is already claimed in Firebase.")
    claim_data = {
        "claimCode": claim_code,
        "claimed": False,
        "enabled": True,
        "deviceId": device["deviceId"],
        "label": device["label"],
        "macAddress": device["macAddress"].upper(),
        "chipId": device["chipId"].upper(),
        "board": device["board"],
        "batch": item.get("batch"),
        "sequence": item.get("sequence"),
        "assignedAt": {".sv": "timestamp"},
    }
    claim_ref.set(claim_data)
    verify = claim_ref.get() or {}
    for field, expected in {
        "claimCode": claim_code,
        "deviceId": device["deviceId"],
        "macAddress": device["macAddress"].upper(),
        "chipId": device["chipId"].upper(),
    }.items():
        if str(verify.get(field, "")).upper() != str(expected).upper():
            fail(f"Firebase claim verification failed for {field}.")
    firebase_db.reference(f"/claimInventory/{claim_code}").set({
        "status": "ASSIGNED",
        "batch": item.get("batch"),
        "sequence": item.get("sequence"),
        "deviceId": device["deviceId"],
        "label": device["label"],
        "macAddress": device["macAddress"].upper(),
        "chipId": device["chipId"].upper(),
        "assignedAt": {".sv": "timestamp"},
    })
    set_claim_firebase_status(claim_code, "VERIFIED")


def build_production(device_id):
    # build_device.py now asks only:
    #   1. Device to build
    #   2. Whether to increment the firmware version
    #
    # FIRST-TIME and historical-learning prompts were removed, so do not pipe
    # extra answers that could be consumed by a future prompt accidentally.
    build_input = f"{device_id}\ny\n"

    run(
        [sys.executable, str(BUILD_SCRIPT)],
        PROJECT_DIR,
        f"Building production firmware for {device_id}...",
        input_text=build_input,
    )


def upload_production(port, board):
    run([str(PIO_EXE), "run", "-e", board, "-t", "upload", "--upload-port", port], PROJECT_DIR, "Uploading production firmware...")


def verify_production(port, device):
    ser = serial.Serial(port, BAUD, timeout=1)
    time.sleep(2.5)
    found_id = found_fw = found_mac = ""
    deadline = time.time() + 90
    try:
        while time.time() < deadline:
            line = ser.readline().decode(errors="ignore").strip()
            if not line:
                continue
            print(line)
            m = re.search(r"DEVICE ID:\s*(\S+)", line)
            if m:
                found_id = m.group(1)
            m = re.search(r"FIRMWARE:\s*(\S+)", line)
            if m:
                found_fw = m.group(1)
            m = re.search(r"MAC Address:\s*([0-9A-Fa-f:]+)", line)
            if m:
                found_mac = m.group(1).upper()
            if found_id and found_fw and found_mac:
                break
    finally:
        ser.close()
    if found_id != device["deviceId"]:
        fail(f"Final device ID mismatch: {found_id}")
    if found_fw != device["firmwareVersion"]:
        fail(f"Final firmware mismatch: {found_fw}")
    if found_mac != device["macAddress"].upper():
        fail(f"Final MAC mismatch: {found_mac}")


def write_label(device, claim_code):
    LABELS_DIR.mkdir(exist_ok=True)
    path = LABELS_DIR / f"{device['deviceId']}.txt"
    path.write_text(
        f"AIDoser\n\nDevice ID: {device['deviceId']}\nSerial Label: {device['label']}\nClaim Code: {claim_code}\nMAC Address: {device['macAddress']}\nChip ID: {device['chipId']}\n\nSetup:\n{SETUP_URL}\n",
        encoding="utf-8",
    )
    return path


def main():
    print("========================================")
    print("      AIDoser Manufacture Device")
    print("========================================")
    port = choose_port()
    upload_burnin(port)
    ser, identity = read_identity(port)
    device = enroll(identity)
    run_burnin(ser, device)

    db = load_devices()
    device = db[device["deviceId"]]
    claim_code, item = reserve_next_claim(device)
    db[device["deviceId"]]["claimCode"] = claim_code
    db[device["deviceId"]]["claimBatch"] = item.get("batch")
    db[device["deviceId"]]["claimSequence"] = item.get("sequence")
    save_devices(db)

    print("\nClaim code assigned from inventory:")
    print(f"  Code:     {claim_code}")
    print(f"  Batch:    {item.get('batch')}")
    print(f"  Sequence: {item.get('sequence')}")

    bind_claim_in_firebase(device, claim_code, item)
    build_production(device["deviceId"])
    db = load_devices()
    device = db[device["deviceId"]]
    upload_production(port, device["board"])
    verify_production(port, device)
    label = write_label(device, claim_code)

    print("\n========================================")
    print("MANUFACTURING COMPLETE")
    print("========================================")
    print(f"Device:           {device['deviceId']}")
    print(f"Label:            {device['label']}")
    print(f"MAC:              {device['macAddress']}")
    print(f"Chip ID:          {device['chipId']}")
    print(f"Firmware:         {device['firmwareVersion']}")
    print(f"Claim code:       {claim_code}")
    print("Claim inventory:  ASSIGNED AND VERIFIED")
    print(f"Label file:       {label}")
    print("\nREADY TO PACKAGE")


if __name__ == "__main__":
    main()