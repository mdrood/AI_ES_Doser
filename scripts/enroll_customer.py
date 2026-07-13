#!/usr/bin/env python3

import json
import os
import serial
import serial.tools.list_ports
import re
from pathlib import Path

DEVICES_FILE = Path(__file__).parent / "devices.json"


def find_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        raise RuntimeError("No serial ports found.")

    if len(ports) == 1:
        return ports[0].device

    print("\nAvailable Ports:")
    for i, p in enumerate(ports):
        print(f"{i+1}. {p.device}   {p.description}")

    choice = int(input("\nSelect Port: "))
    return ports[choice-1].device


def read_device(port):

    ser = serial.Serial(port,115200,timeout=5)

    mac = None
    chip = None
    board = "vintlabs-devkit-v1"

    print("\nReading device...\n")

    for _ in range(80):

        line = ser.readline().decode(errors="ignore").strip()

        print(line)

        m = re.search(r"MAC Address:\s*([0-9A-Fa-f:]+)",line)
        if m:
            mac = m.group(1)

        c = re.search(r"Chip ID:\s*([0-9A-Fa-f]+)",line)
        if c:
            chip = c.group(1)

        b = re.search(r"Board:\s*(.+)",line)
        if b:
            board = b.group(1)

        if mac and chip:
            break

    ser.close()

    if not mac:
        raise RuntimeError("Could not read MAC Address.")

    if not chip:
        raise RuntimeError("Could not read Chip ID.")

    return mac,chip,board


def load_database():

    if DEVICES_FILE.exists():
        with open(DEVICES_FILE,"r") as f:
            return json.load(f)

    return {}


def save_database(data):

    with open(DEVICES_FILE,"w") as f:
        json.dump(data,f,indent=4)


def main():

    port = find_port()

    mac,chip,board = read_device(port)

    print("\n---------------------------")
    print("Detected Hardware")
    print("---------------------------")
    print("MAC :",mac)
    print("Chip:",chip)
    print("Board:",board)
    print()

    device = input("Device ID (reefDoser1): ").strip()

    customer = input("Customer Name: ").strip()

    label = input("Label (AIES-00017): ").strip()

    version = input("Firmware Version: ").strip()

    db = load_database()

    db[device] = {

        "customer": customer,
        "label": label,

        "deviceId": device,
        "firmwareVersion": version,

        "chipId": chip,
        "macAddress": mac,

        "board": board,

        "otaJsonName": f"{device}.json",
        "otaBinName": f"{device}.bin",

        "firebasePublic":
        "C:/Users/mdroo/OneDrive/Firebase/aiesdoser/public"
    }

    save_database(db)

    print("\n✓ Device enrolled successfully.")
    print("Updated:",DEVICES_FILE)


if __name__ == "__main__":
    main()