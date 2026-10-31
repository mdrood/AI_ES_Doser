#!/usr/bin/env python3
"""
Point an existing claim-inventory assignment at a different physical chip.

Use this when manufacture_device.py stops with:
    ERROR: Existing claim assignment identity mismatch for <deviceId>.

It copies the MAC address and chip ID that manufacture_device.py already saved
for <deviceId> in scripts/devices.json (i.e. the NEW chip) into the matching
ASSIGNED entry of claim_inventory/claim_inventory.json. The claim code does not
change. A .bak copy of the inventory is written first.

Run from the scripts folder:
    python reassign_claim.py reefDoser14

Only MAC / chip ID values are printed. Nothing else from devices.json is shown.
"""
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[1]
DEVICES_FILE = PROJECT_DIR / "scripts" / "devices.json"
CLAIM_INVENTORY_FILE = PROJECT_DIR / "claim_inventory" / "claim_inventory.json"


def fail(msg):
    print(f"\nERROR: {msg}")
    sys.exit(1)


def main():
    if len(sys.argv) != 2:
        fail("Usage: python reassign_claim.py <deviceId>   (e.g. reefDoser14)")
    device_id = sys.argv[1].strip()

    if not DEVICES_FILE.exists():
        fail(f"Not found: {DEVICES_FILE}")
    if not CLAIM_INVENTORY_FILE.exists():
        fail(f"Not found: {CLAIM_INVENTORY_FILE}")

    devices = json.loads(DEVICES_FILE.read_text(encoding="utf-8"))
    device = devices.get(device_id)
    if not device:
        fail(f"{device_id} is not in devices.json.")
    new_mac = str(device.get("macAddress", "")).upper()
    new_chip = str(device.get("chipId", "")).upper()
    if not new_mac or not new_chip:
        fail(f"{device_id} in devices.json has no macAddress/chipId.")

    inventory = json.loads(CLAIM_INVENTORY_FILE.read_text(encoding="utf-8"))
    matches = [
        item for item in inventory.get("claims", [])
        if item.get("status") == "ASSIGNED" and item.get("deviceId") == device_id
    ]
    if len(matches) == 0:
        fail(f"No ASSIGNED claim for {device_id} in the inventory. Nothing to reassign.")
    if len(matches) > 1:
        fail(f"{len(matches)} ASSIGNED claims for {device_id}; fix the inventory by hand first.")
    item = matches[0]

    old_mac = str(item.get("macAddress", "")).upper()
    old_chip = str(item.get("chipId", "")).upper()
    print(f"Device:        {device_id}")
    print(f"Claim record:  MAC {old_mac}   Chip {old_chip}")
    print(f"New chip:      MAC {new_mac}   Chip {new_chip}")

    if old_mac == new_mac and old_chip == new_chip:
        print("\nAlready matching. Nothing to change.")
        return

    print("\nThe OLD chip will no longer match this device's claim after this.")
    print("Only continue if the old chip is not out in the field.")
    if input("Type YES to reassign the claim to the new chip: ").strip() != "YES":
        fail("Cancelled. Nothing changed.")

    CLAIM_INVENTORY_FILE.with_suffix(".json.bak").write_bytes(CLAIM_INVENTORY_FILE.read_bytes())
    item["macAddress"] = new_mac
    item["chipId"] = new_chip
    item["firebaseStatus"] = "PENDING"
    item["reassignedAt"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
    item["previousMacAddress"] = old_mac
    item["previousChipId"] = old_chip
    inventory["updatedAt"] = item["reassignedAt"]
    CLAIM_INVENTORY_FILE.write_text(json.dumps(inventory, indent=4) + "\n", encoding="utf-8")
    print("\nDone. Backup saved as claim_inventory.json.bak.")
    print("Now run manufacture_device.py again.")


if __name__ == "__main__":
    main()
