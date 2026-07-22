#!/usr/bin/env python3
import json, re, sys, time
from datetime import datetime
from pathlib import Path
import serial
import serial.tools.list_ports

PROJECT_DIR = Path(__file__).resolve().parents[1]
DEVICES_FILE = PROJECT_DIR / "scripts" / "devices.json"
REPORTS_DIR = PROJECT_DIR / "burn_in_reports"
DEFAULT_FIREBASE_PUBLIC = "C:/Users/mdroo/OneDrive/Firebase/aiesdoser/public"

def fail(msg):
    print(f"\nERROR: {msg}")
    sys.exit(1)

def choose_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports: fail("No serial ports found.")
    if len(ports) == 1: return ports[0].device
    for i,p in enumerate(ports,1): print(f"{i}. {p.device}  {p.description}")
    while True:
        try:
            n=int(input("Select port number: "))
            if 1 <= n <= len(ports): return ports[n-1].device
        except ValueError: pass

def read_identity(ser):
    ser.write(b"IDENTITY\n"); ser.flush()
    end=time.time()+20; mac=chip=None; board="vintlabs-devkit-v1"
    while time.time()<end:
        line=ser.readline().decode(errors="ignore").strip()
        if not line: continue
        print(line)
        m=re.search(r"MAC Address:\s*([0-9A-Fa-f:]+)",line)
        if m: mac=m.group(1).upper()
        m=re.search(r"Chip ID:\s*([0-9A-Fa-f]+)",line)
        if m: chip=m.group(1).upper()
        m=re.search(r"Board:\s*(.+)",line)
        if m: board=m.group(1).strip()
        if mac and chip: return mac,chip,board
    fail("Could not read identity. Is burn-in firmware installed?")

def load_devices():
    return json.loads(DEVICES_FILE.read_text()) if DEVICES_FILE.exists() else {}

def save_devices(data):
    if DEVICES_FILE.exists(): DEVICES_FILE.with_suffix('.json.bak').write_bytes(DEVICES_FILE.read_bytes())
    DEVICES_FILE.write_text(json.dumps(data,indent=4)+"\n")

def ask(label, default):
    v=input(f"{label} [{default}]: ").strip()
    return v or default

def cmd(ser,s):
    ser.write((s+"\n").encode()); ser.flush()

def main():
    print("AIDoser Factory Burn-In")
    port=choose_port(); ser=serial.Serial(port,115200,timeout=1); time.sleep(2.5); ser.reset_input_buffer()
    try:
        mac,chip,board=read_identity(ser)
        print(f"Detected MAC={mac} Chip={chip} Board={board}")
        device=ask("Device ID","reefDoser4")
        customer=ask("Customer name","Burn-In Pending")
        label=ask("Label","AIES-00004")
        version=ask("Firmware version","T-1.0.0")
        duration=int(ask("Burn-in duration minutes","120"))
        db=load_devices()
        if device in db and input(f"{device} exists. Overwrite? [y/N]: ").lower() not in ('y','yes'): fail("Cancelled")
        db[device]={
            "customer":customer,"label":label,"deviceId":device,"firmwareVersion":version,
            "chipId":chip,"macAddress":mac,"board":board,
            "otaJsonName":f"{device}.json","otaBinName":f"{device}.bin",
            "firebasePublic":DEFAULT_FIREBASE_PUBLIC,
            "burnInStatus":"RUNNING","burnInStartedAt":datetime.now().isoformat(timespec='seconds')
        }
        save_devices(db)
        cmd(ser,f"SET_DEVICE_ID {device}"); cmd(ser,f"SET_DURATION_MIN {duration}"); cmd(ser,"START")
        events=[]; result="FAIL"; runs=0; started=datetime.now(); reason=""
        try:
            while True:
                line=ser.readline().decode(errors="ignore").strip()
                if not line: continue
                print(line); events.append({"time":datetime.now().isoformat(timespec='seconds'),"line":line})
                m=re.search(r"completedRuns=(\d+)",line)
                if m: runs=max(runs,int(m.group(1)))
                if line.startswith("BURNIN_COMPLETE"):
                    result="PASS" if "result=PASS" in line else "FAIL"; reason="" if result=="PASS" else line; break
                if line.startswith("ERROR"): reason=line; break
        except KeyboardInterrupt:
            cmd(ser,"STOP"); reason="Operator cancelled"
        completed=datetime.now(); REPORTS_DIR.mkdir(exist_ok=True)
        report={"deviceId":device,"customer":customer,"label":label,"macAddress":mac,"chipId":chip,"board":board,
                "startedAt":started.isoformat(timespec='seconds'),"completedAt":completed.isoformat(timespec='seconds'),
                "durationMinutesRequested":duration,"completedPumpRuns":runs,"result":result,"failureReason":reason,"events":events}
        rp=REPORTS_DIR/f"{device}_{completed:%Y%m%d_%H%M%S}.json"; rp.write_text(json.dumps(report,indent=4)+"\n")
        db=load_devices(); db[device]["burnInStatus"]=result; db[device]["burnInCompletedAt"]=completed.isoformat(timespec='seconds'); save_devices(db)
        print(f"Burn-in result: {result}\nReport: {rp}")
    finally:
        try: cmd(ser,"STOP")
        except Exception: pass
        ser.close()

if __name__ == '__main__': main()
