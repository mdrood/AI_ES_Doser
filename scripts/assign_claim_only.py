#!/usr/bin/env python3
"""
Assign (or look up) a claim code for ONE already-flashed device, WITHOUT
re-flashing, burn-in, or rebuilding. Uses the same functions and the same
Firebase layout as manufacture_device.py, so the result is identical to what a
normal manufacture run would have written.

Put this file next to manufacture_device.py (the scripts folder) and run:
    python scripts\\assign_claim_only.py reefDoser3

Safe to re-run: if the device already has a claim code in claim_inventory.json
it reuses that code, and it refuses to touch a device that is already claimed
in Firebase.
"""
import sys

import manufacture_device as m


def main():
    if len(sys.argv) < 2:
        m.fail("Usage: python scripts\\assign_claim_only.py <deviceId>   e.g. reefDoser3")
    device_id = sys.argv[1].strip()

    db = m.load_devices()
    device = db.get(device_id)
    if device is None:
        print(f"{device_id} is not in devices.json. Enter its details "
              f"(MAC and chip ID are printed in the device's boot log).")
        device = {
            "deviceId": device_id,
            "label": m.prompt("Label", "AIES-TEST"),
            "macAddress": m.prompt("MAC address (AA:BB:CC:DD:EE:FF)", "").upper(),
            "chipId": m.prompt("Chip ID", "").upper(),
            "board": m.prompt("Board", "vintlabs-devkit-v1"),
        }
        if not device["macAddress"] or not device["chipId"]:
            m.fail("MAC address and chip ID are required.")

    claim_code, item = m.reserve_next_claim(device)
    print("\nClaim code for this device:")
    print(f"  Device:   {device_id}")
    print(f"  Code:     {claim_code}")
    print(f"  Batch:    {item.get('batch')}")
    print(f"  Sequence: {item.get('sequence')}")

    m.bind_claim_in_firebase(device, claim_code, item)

    if device_id in db:
        db[device_id]["claimCode"] = claim_code
        db[device_id]["claimBatch"] = item.get("batch")
        db[device_id]["claimSequence"] = item.get("sequence")
        m.save_devices(db)

    label = m.write_label(device, claim_code)
    print("\nDONE. Firebase now has:")
    print(f"  /deviceClaims/{device_id}  (claimed=false)")
    print(f"  /claimInventory/{claim_code}  (status=ASSIGNED)")
    print(f"Label file: {label}")
    print(f"\nUse this claim code on the setup page: {claim_code}")


if __name__ == "__main__":
    main()
