#!/usr/bin/env python3

import hashlib
import json
import re
import secrets
import string
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[1]

# Enrollment script writes here
DEVICES_FILE = PROJECT_DIR / "scripts" / "devices.json"

GENERATED_DIR = PROJECT_DIR / "generated"
GENERATED_HEADER = GENERATED_DIR / "device_config.generated.h"

PIO_EXE = r"C:\Users\mdroo\.platformio\penv\Scripts\platformio.exe"
DEFAULT_PIO_ENV = "vintlabs-devkit-v1"

SERVICE_ACCOUNT_FILE = PROJECT_DIR / "secrets" / "firebase-service-account.json"
DEVICE_EMAIL_DOMAIN = "device.aidoser.tech"

REQUIRED_FIELDS = [
    "customer",
    "label",
    "deviceId",
    "firmwareVersion",
    "chipId",
    "macAddress",
    "board",
    "otaJsonName",
    "otaBinName",
    "firebasePublic",
]


def fail(msg):
    print(f"\nERROR: {msg}")
    sys.exit(1)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_devices():
    if not DEVICES_FILE.exists():
        fail(f"devices.json not found:\n{DEVICES_FILE}")

    try:
        return json.loads(DEVICES_FILE.read_text(encoding="utf-8"))
    except Exception as e:
        fail(f"Could not read devices.json: {e}")


def save_devices(devices):
    try:
        DEVICES_FILE.write_text(json.dumps(devices, indent=4) + "\n", encoding="utf-8")
    except Exception as e:
        fail(f"Could not write devices.json: {e}")


def increment_firmware_version(version):
    """
    Increment the last numeric component while preserving the prefix/separators.

    Examples:
      T-1.0.0    -> T-1.0.1
      P_1.1.5    -> P_1.1.6
      10.7.0     -> 10.7.1
      T-1.0.0.9  -> T-1.0.0.10
    """
    v = str(version).strip()
    m = re.search(r"(\d+)(?!.*\d)", v)
    if not m:
        fail(f"Cannot auto-increment firmwareVersion because it has no number: {version}")

    start, end = m.span(1)
    n = int(m.group(1)) + 1
    return v[:start] + str(n) + v[end:]



def cpp_escape(value):
    return str(value).replace("\\", "\\\\").replace('"', '\\"')


def generate_device_password(length=24):
    alphabet = string.ascii_letters + string.digits + "!@#$%^&*-_"

    while True:
        password = "".join(secrets.choice(alphabet) for _ in range(length))

        if (
            any(c.islower() for c in password)
            and any(c.isupper() for c in password)
            and any(c.isdigit() for c in password)
            and any(c in "!@#$%^&*-_" for c in password)
        ):
            return password


def ensure_firebase_admin():
    try:
        import firebase_admin
        from firebase_admin import auth as firebase_auth
        from firebase_admin import credentials
    except ImportError:
        fail(
            "firebase-admin is not installed.\n\n"
            f'Run:\n"{sys.executable}" -m pip install firebase-admin'
        )

    if not SERVICE_ACCOUNT_FILE.exists():
        fail(
            "Firebase service-account key not found.\n\n"
            f"Expected:\n{SERVICE_ACCOUNT_FILE}"
        )

    if not firebase_admin._apps:
        cred = credentials.Certificate(str(SERVICE_ACCOUNT_FILE))
        firebase_admin.initialize_app(cred)

    return firebase_auth


def ensure_device_firebase_credentials(devices, device_name, d):
    firebase_auth = ensure_firebase_admin()

    changed = False
    email = str(d.get("firebaseEmail", "")).strip()
    password = str(d.get("firebasePassword", "")).strip()
    uid = str(d.get("firebaseUid", "")).strip()

    if not email:
        email = f'{d["deviceId"]}@{DEVICE_EMAIL_DOMAIN}'
        d["firebaseEmail"] = email
        changed = True

    if not password:
        password = generate_device_password()
        d["firebasePassword"] = password
        changed = True

    if len(password) < 8:
        fail(
            f'firebasePassword for {d["deviceId"]} must be at least 8 characters.'
        )

    try:
        user = firebase_auth.get_user_by_email(email)

        if uid and uid != user.uid:
            fail(
                f"Firebase UID mismatch for {email}.\n"
                f"devices.json: {uid}\n"
                f"Firebase Auth: {user.uid}"
            )

        if not uid:
            d["firebaseUid"] = user.uid
            changed = True

        firebase_auth.update_user(
            user.uid,
            password=password,
            disabled=False,
        )

        print("\nFirebase device Auth user verified:")
        print(f"  email : {email}")
        print(f"  uid   : {user.uid}")

    except firebase_auth.UserNotFoundError:
        user = firebase_auth.create_user(
            email=email,
            password=password,
            email_verified=True,
            disabled=False,
            display_name=d["deviceId"],
        )

        d["firebaseUid"] = user.uid
        changed = True

        print("\nFirebase device Auth user created:")
        print(f"  email : {email}")
        print(f"  uid   : {user.uid}")

    devices[device_name] = d

    if changed:
        save_devices(devices)
        print(f"  credentials saved to: {DEVICES_FILE}")

    return d


def validate_device(d):
    missing = [k for k in REQUIRED_FIELDS if k not in d or str(d[k]).strip() == ""]
    if missing:
        fail("Device entry is missing required fields: " + ", ".join(missing))

    if d["deviceId"] not in d["otaBinName"]:
        fail(f'otaBinName should contain deviceId. Got: {d["otaBinName"]}')

    if d["deviceId"] not in d["otaJsonName"]:
        fail(f'otaJsonName should contain deviceId. Got: {d["otaJsonName"]}')


def write_generated_header(d, password_override=None):
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)

    # password_override lets a staged credential rotation build firmware with
    # a NEW password baked in, without that password being live in Firebase
    # Auth yet (see rotate_start/rotate_finalize). Normal builds never pass
    # this and continue to embed the current, already-live firebasePassword.
    password = password_override if password_override is not None else d["firebasePassword"]

    header = f'''#pragma once

#include <Arduino.h>

// AUTO-GENERATED by scripts/build_device.py
// Do not edit this file manually.

const String BUILD_DEVICE_ID = "{d["deviceId"]}";
const String BUILD_DEVICE_LABEL = "{d["label"]}";
const String BUILD_FW_VERSION = "{d["firmwareVersion"]}";

const String BUILD_EXPECTED_MAC = "{d["macAddress"]}";
const String BUILD_EXPECTED_CHIP = "{d["chipId"]}";
const String BUILD_EXPECTED_BOARD = "{d["board"]}";
const String BUILD_FIREBASE_UID = "{cpp_escape(d["firebaseUid"])}";

#define BUILD_FIREBASE_EMAIL "{cpp_escape(d["firebaseEmail"])}"
#define BUILD_FIREBASE_PASSWORD "{cpp_escape(password)}"

#define BUILD_ALK_DEMAND_BOOTSTRAP_ENABLED {1 if d.get("alkDemandBootstrapEnabled", False) else 0}
#define BUILD_ALK_DEMAND_BOOTSTRAP_DKH_DAY {float(d.get("alkDemandBootstrapDkhDay", 0.0)):.6f}f
'''

    GENERATED_HEADER.write_text(header, encoding="utf-8")
    return GENERATED_HEADER


def run_platformio(pio_env):
    print("\nRunning PlatformIO build...")
    result = subprocess.run(
        [PIO_EXE, "run", "-e", pio_env],
        cwd=PROJECT_DIR,
    )

    if result.returncode != 0:
        fail("PlatformIO build failed.")


def ask_yes_no(prompt, default=False):
    suffix = " [Y/n]: " if default else " [y/N]: "

    while True:
        try:
            answer = input(prompt + suffix).strip().lower()
        except EOFError:
            print(
                f"\nNo interactive input available for: {prompt}\n"
                f"Using default: {'Yes' if default else 'No'}"
            )
            return default

        if answer == "":
            return default

        if answer in ("y", "yes"):
            return True

        if answer in ("n", "no"):
            return False

        print("Please answer y or n.")


def ask_float(prompt, default=None, minimum=0.0):
    while True:
        suffix = f" [{default}]: " if default is not None else ": "
        answer = input(prompt + suffix).strip()

        if answer == "" and default is not None:
            value = float(default)
        else:
            try:
                value = float(answer)
            except ValueError:
                print("Please enter a valid number.")
                continue

        if value < minimum:
            print(f"Value must be at least {minimum}.")
            continue

        return value


def make_ota_json(d, dist_bin, ota_bin_name=None, ota_json_name=None):
    device_id = d["deviceId"]
    bin_name = ota_bin_name or d["otaBinName"]
    json_name = ota_json_name or d["otaJsonName"]

    firmware_path = f"/devices/{device_id}/{bin_name}"
    firmware_url = f"https://aiesdoser.web.app{firmware_path}"

    return {
        "ok": True,
        "deviceId": device_id,
        "customer": d["customer"],
        "label": d["label"],
        "firmwareVersion": d["firmwareVersion"],
        "version": d["firmwareVersion"],
        "board": d["board"],
        "chipId": d["chipId"],
        "macAddress": d["macAddress"],
        "otaBinName": bin_name,
        "otaJsonName": json_name,
        "firmwarePath": firmware_path,
        "firmwareUrl": firmware_url,
        "binUrl": firmware_url,
        "size": dist_bin.stat().st_size,
        "sha256": sha256_file(dist_bin),
        "builtAt": datetime.now().isoformat(timespec="seconds"),
    }


def copy_and_verify(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)

    if not dst.exists():
        fail(f"Copy failed, file missing:\n{dst}")

    if src.stat().st_size != dst.stat().st_size:
        fail(f"Copy failed, size mismatch:\nSRC: {src}\nDST: {dst}")


def main():
    print("========================================")
    print("      AIES DEVICE BUILDER v3")
    print("========================================")

    devices = load_devices()

    print("\nAvailable devices:")
    for name in devices:
        print(f"  - {name}")

    device_name = input("\nDevice to build: ").strip()

    if device_name not in devices:
        fail(f"Unknown device: {device_name}")

    d = devices[device_name]
    validate_device(d)
    d = ensure_device_firebase_credentials(devices, device_name, d)

    old_fw_version = str(d["firmwareVersion"]).strip()
    bump_version = ask_yes_no(
        f'Increment firmware version for this build? {old_fw_version} -> {increment_firmware_version(old_fw_version)}',
        default=True,
    )

    if bump_version:
        new_fw_version = increment_firmware_version(old_fw_version)
        d["firmwareVersion"] = new_fw_version
        devices[device_name] = d
        save_devices(devices)
        print(f"\nFirmware version incremented:")
        print(f"  {old_fw_version} -> {new_fw_version}")
        print(f"  saved to: {DEVICES_FILE}")
    else:
        print(f"\nFirmware version unchanged: {old_fw_version}")

    validate_device(d)


    # Manufacturing builds must not stop for an Alk-bootstrap prompt.
    # Preserve an explicitly configured seed already stored for this device.
    # New devices default to no one-time bootstrap while still receiving
    # normal rolling 7-day Alk and calcium learning.
    bootstrap_enabled = bool(d.get("alkDemandBootstrapEnabled", False))
    bootstrap_dkh_day = float(d.get("alkDemandBootstrapDkhDay", 0.0) or 0.0)

    if not bootstrap_enabled:
        bootstrap_dkh_day = 0.0
    elif bootstrap_dkh_day <= 0.0:
        fail(
            "alkDemandBootstrapEnabled is true but "
            "alkDemandBootstrapDkhDay is missing or not positive."
        )

    d["alkDemandBootstrapEnabled"] = bootstrap_enabled
    d["alkDemandBootstrapDkhDay"] = bootstrap_dkh_day
    devices[device_name] = d
    save_devices(devices)

    pio_env = d.get("board", DEFAULT_PIO_ENV) or DEFAULT_PIO_ENV

    build_and_package(d, pio_env)

    print("\n========================================")
    print("BUILD COMPLETE - READY FOR FIREBASE DEPLOY")
    print("========================================")


def build_and_package(d, pio_env, password_override=None):
    """Compile firmware for device d and stage it for OTA deploy.

    password_override, if given, embeds that password in the built firmware
    WITHOUT changing d["firebasePassword"] or touching Firebase Auth. This is
    what a staged credential rotation uses (see rotate_start) so the new
    firmware can be built and OTA-pushed while the OLD password is still the
    one Firebase Auth accepts, keeping the device reachable the whole time.
    """
    header_path = write_generated_header(d, password_override=password_override)

    print("\nGenerated:")
    print(header_path)

    print("\nDevice:")
    print(f'  deviceId : {d["deviceId"]}')
    print(f'  customer : {d["customer"]}')
    print(f'  label    : {d["label"]}')
    print(f'  version  : {d["firmwareVersion"]}')
    print(f'  mac      : {d["macAddress"]}')
    print(f'  chip     : {d["chipId"]}')
    print(f'  board    : {d["board"]}')
    print(f'  auth     : {d["firebaseEmail"]}')
    print(f'  auth UID : {d["firebaseUid"]}')
    print(f'  alk seed : {"ENABLED" if d.get("alkDemandBootstrapEnabled", False) else "disabled"}')
    if d.get("alkDemandBootstrapEnabled", False):
        print(f'  seed dKH : {float(d.get("alkDemandBootstrapDkhDay", 0.0)):.3f} dKH/day')
    if password_override is not None:
        print("  auth pw  : STAGED (pending) - Firebase Auth NOT yet updated")

    run_platformio(pio_env)

    print("\nBUILD SUCCESS")

    build_bin = PROJECT_DIR / ".pio" / "build" / pio_env / "firmware.bin"

    if not build_bin.exists():
        fail(f"firmware.bin not found:\n{build_bin}")

    device_id = d["deviceId"]

    # Local release folder:
    # dist/reefDoser3/reefDoser3.bin
    # dist/reefDoser3/reefDoser3.json
    dist_dir = PROJECT_DIR / "dist" / device_id
    dist_bin = dist_dir / d["otaBinName"]
    dist_json = dist_dir / d["otaJsonName"]

    dist_dir.mkdir(parents=True, exist_ok=True)
    copy_and_verify(build_bin, dist_bin)

    ota = make_ota_json(d, dist_bin)
    dist_json.write_text(json.dumps(ota, indent=4), encoding="utf-8")

    if not dist_json.exists():
        fail(f"OTA JSON was not created:\n{dist_json}")


    # Firebase public device folder:
    # C:/Users/mdroo/OneDrive/Firebase/aiesdoser/public/devices/reefDoser3/reefDoser3.bin
    # C:/Users/mdroo/OneDrive/Firebase/aiesdoser/public/devices/reefDoser3/reefDoser3.json
    firebase_public = Path(d["firebasePublic"])
    firebase_device_dir = firebase_public / "devices" / device_id
    firebase_bin = firebase_device_dir / d["otaBinName"]
    firebase_json = firebase_device_dir / d["otaJsonName"]

    copy_and_verify(dist_bin, firebase_bin)
    copy_and_verify(dist_json, firebase_json)


    print("\nFirmware copied to:")
    print(f"  {dist_bin}")

    print("\nOTA JSON created:")
    print(f"  {dist_json}")

    print("\nFirebase files copied to:")
    print(f"  {firebase_bin}")
    print(f"  {firebase_json}")


    print("\nOTA URL paths:")
    print(f'  JSON: /devices/{device_id}/{d["otaJsonName"]}')
    print(f'  BIN : /devices/{device_id}/{d["otaBinName"]}')


    print("\nVerification:")
    print(f'  size   : {ota["size"]}')
    print(f'  sha256 : {ota["sha256"]}')

    return dist_bin, dist_json


def rotate_start(devices, device_name):
    """Phase 1 of a safe credential rotation.

    Generates a NEW password and builds/stages firmware with it, but does
    NOT touch Firebase Auth. The currently-deployed device keeps using its
    existing (still-valid) password/firmware the entire time, so it stays
    reachable for the OTA push. Only rotate_finalize() actually changes what
    Firebase Auth accepts.
    """
    if device_name not in devices:
        fail(f"Unknown device: {device_name}")

    d = devices[device_name]
    validate_device(d)

    if str(d.get("firebasePasswordPending", "")).strip():
        fail(
            f"A rotation is already staged for {device_name}.\n"
            f"Finalize it (rotate-finalize) or discard it (rotate-abort) first."
        )

    if not str(d.get("firebasePassword", "")).strip():
        fail(
            f"{device_name} has no current firebasePassword on file.\n"
            f"This looks like a brand-new device - use the normal build flow "
            f"(no arguments) instead, which will provision it directly."
        )

    # Confirm we can actually reach Firebase Admin before generating anything,
    # so a missing/broken service account fails fast instead of mid-rotation.
    ensure_firebase_admin()

    new_password = generate_device_password()
    d["firebasePasswordPending"] = new_password
    devices[device_name] = d
    save_devices(devices)

    old_fw_version = str(d["firmwareVersion"]).strip()
    bump_version = ask_yes_no(
        f'Increment firmware version for this build? {old_fw_version} -> {increment_firmware_version(old_fw_version)}',
        default=True,
    )

    if bump_version:
        new_fw_version = increment_firmware_version(old_fw_version)
        d["firmwareVersion"] = new_fw_version
        devices[device_name] = d
        save_devices(devices)
        print(f"\nFirmware version incremented:")
        print(f"  {old_fw_version} -> {new_fw_version}")
    else:
        print(f"\nFirmware version unchanged: {old_fw_version}")

    pio_env = d.get("board", DEFAULT_PIO_ENV) or DEFAULT_PIO_ENV

    print(f"\nStaging new credential for {device_name}.")
    print("Firebase Auth is NOT changed yet - the currently-deployed device")
    print("keeps working with its existing password throughout this build.")

    build_and_package(d, pio_env, password_override=new_password)

    print("\n========================================")
    print("ROTATION STAGED - Firebase Auth unchanged")
    print("========================================")
    print(f"\nNext steps for {device_name}:")
    print("  1. Trigger the OTA update for this device as usual.")
    print("  2. Confirm it actually rebooted onto the new build - watch for")
    print("     the new firmware version being pushed to Firebase, or")
    print("     'Firebase token ready.' in the serial monitor after reboot.")
    print("  3. Only once you've confirmed that, run:")
    print(f"       python build_device.py rotate-finalize {device_name}")
    print("     This is the step that actually changes the password Firebase")
    print("     Auth requires. Doing it before step 2 is confirmed is what")
    print("     causes a device lockout.")


def rotate_finalize(devices, device_name):
    """Phase 2 of a safe credential rotation.

    Pushes the already-staged password live to Firebase Auth. Only run this
    after confirming the device is already running firmware built with that
    same staged password (see rotate_start).
    """
    if device_name not in devices:
        fail(f"Unknown device: {device_name}")

    d = devices[device_name]
    pending = str(d.get("firebasePasswordPending", "")).strip()

    if not pending:
        fail(
            f"No staged rotation found for {device_name}.\n"
            f"Run 'python build_device.py rotate-start {device_name}' first."
        )

    confirmed = ask_yes_no(
        f"Have you CONFIRMED {device_name} is already running firmware "
        f"built with the staged password (rebooted + reconnected to "
        f"Firebase successfully)?",
        default=False,
    )

    if not confirmed:
        print(
            "\nStopping. Finalize only after the device has confirmed it's "
            "running the new build - otherwise this will lock it out, the "
            "same way the previous incident happened."
        )
        return

    firebase_auth = ensure_firebase_admin()
    email = d["firebaseEmail"]
    user = firebase_auth.get_user_by_email(email)

    uid = str(d.get("firebaseUid", "")).strip()
    if uid and uid != user.uid:
        fail(
            f"Firebase UID mismatch for {email}.\n"
            f"devices.json: {uid}\n"
            f"Firebase Auth: {user.uid}"
        )

    firebase_auth.update_user(user.uid, password=pending, disabled=False)

    d["firebasePassword"] = pending
    d["firebasePasswordPending"] = ""
    devices[device_name] = d
    save_devices(devices)

    print(f"\nRotation finalized for {device_name}.")
    print("Firebase Auth now requires the new password - this should already")
    print("match what the device is running, so it will keep working with no")
    print("further action needed.")


def rotate_abort(devices, device_name):
    """Discard a staged (not-yet-finalized) rotation. Firebase Auth and the
    currently-deployed device are untouched either way."""
    if device_name not in devices:
        fail(f"Unknown device: {device_name}")

    d = devices[device_name]

    if not str(d.get("firebasePasswordPending", "")).strip():
        fail(f"No staged rotation to abort for {device_name}.")

    d["firebasePasswordPending"] = ""
    devices[device_name] = d
    save_devices(devices)

    print(f"\nStaged rotation for {device_name} discarded.")
    print("Firebase Auth and the currently-deployed firmware are unaffected.")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("rotate-start", "rotate-finalize", "rotate-abort"):
        command = sys.argv[1]
        devices = load_devices()

        device_name = sys.argv[2] if len(sys.argv) > 2 else input("Device: ").strip()

        if command == "rotate-start":
            rotate_start(devices, device_name)
        elif command == "rotate-finalize":
            rotate_finalize(devices, device_name)
        elif command == "rotate-abort":
            rotate_abort(devices, device_name)
    else:
        main()