import sys, subprocess, threading, queue, struct
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from serial.tools import list_ports

BASE = Path(__file__).resolve().parent
CHIP_ID = 9  # ESP32-S3 image header chip ID
FLASH_BYTES = 8 * 1024 * 1024

def check_image(path):
    data = Path(path).read_bytes()
    if len(data) < 24 or data[0] != 0xE9:
        raise ValueError(f'{Path(path).name} is not an ESP32 firmware image.')
    chip = int.from_bytes(data[12:14], 'little')
    if chip != CHIP_ID:
        raise ValueError(f'{Path(path).name} targets chip ID {chip}, not ESP32-S3 (9).')
    return len(data)

def partitions():
    raw = (BASE / 'partitions.bin').read_bytes()
    found = {}
    for i in range(0, len(raw)-31, 32):
        rec = raw[i:i+32]
        if rec[:2] != b'\xaa\x50':
            break
        _, typ, subtype, offset, size, label, flags = struct.unpack('<HBBII16sI', rec)
        name = label.split(b'\x00')[0].decode('ascii', 'replace')
        found[name] = (typ, subtype, offset, size)
        if offset + size > FLASH_BYTES:
            raise ValueError(f'Partition {name} exceeds 8 MB flash.')
    if 'app0' not in found or 'otadata' not in found:
        raise ValueError('Expected app0 and otadata partitions are missing.')
    return found

class Installer:
    def __init__(self, root):
        self.root = root
        root.title('AIDoser ESP32-S3 Firmware Installer')
        root.geometry('780x550')
        self.port = tk.StringVar()
        self.firmware = tk.StringVar(value=str(BASE / 'firmware.bin'))
        self.status = tk.StringVar(value='Ready. Connect ESP-PROG-2 and select the COM port.')
        self.q = queue.Queue()
        self.busy = False
        f = ttk.Frame(root, padding=16); f.pack(fill='both', expand=True)
        ttk.Label(f, text='AIDoser Firmware Installer', font=('Segoe UI', 18, 'bold')).pack(anchor='w')
        ttk.Label(f, text='ESP32-S3-WROOM-1-N8R8  |  8 MB flash / 8 MB PSRAM  |  ESP-PROG-2').pack(anchor='w', pady=(0,15))
        p = ttk.Frame(f); p.pack(fill='x')
        ttk.Label(p, text='COM port:').pack(side='left')
        self.ports = ttk.Combobox(p, textvariable=self.port, width=53, state='readonly'); self.ports.pack(side='left', padx=8)
        ttk.Button(p, text='Refresh', command=self.refresh).pack(side='left')
        x = ttk.LabelFrame(f, text='Firmware file (select a new firmware.bin for future updates)', padding=10)
        x.pack(fill='x', pady=12)
        ttk.Entry(x, textvariable=self.firmware).pack(side='left', fill='x', expand=True)
        ttk.Button(x, text='Browse...', command=self.browse).pack(side='left', padx=(8,0))
        b = ttk.Frame(f); b.pack(fill='x', pady=5)
        self.factory = ttk.Button(b, text='FACTORY INSTALL (ERASE ALL)', command=lambda: self.start('factory'))
        self.factory.pack(side='left', padx=(0,10))
        self.update = ttk.Button(b, text='UPDATE FIRMWARE (KEEP SETTINGS)', command=lambda: self.start('update'))
        self.update.pack(side='left')
        ttk.Label(f, text='Factory install deletes Wi-Fi, device identity, calibration, settings and stored data. Firmware update keeps NVS and SPIFFS/LittleFS, but requires compatible partition layout.', wraplength=730).pack(anchor='w', pady=(8,8))
        self.progress = ttk.Progressbar(f, mode='indeterminate'); self.progress.pack(fill='x', pady=(0,5))
        ttk.Label(f, textvariable=self.status, wraplength=730).pack(anchor='w')
        self.log = tk.Text(f, height=13, wrap='word', state='disabled'); self.log.pack(fill='both', expand=True, pady=(10,0))
        self.refresh(); root.after(100, self.poll)

    def refresh(self):
        ports = list(list_ports.comports())
        self.devices = [p.device for p in ports]
        self.ports['values'] = [f'{p.device} — {p.description}' for p in ports]
        if ports: self.ports.current(0)
        else: self.port.set('')

    def browse(self):
        v = filedialog.askopenfilename(title='Select ESP32-S3 AIDoser firmware.bin', filetypes=[('BIN files', '*.bin')])
        if v: self.firmware.set(v)

    def logline(self, s): self.q.put(('log', s))

    def start(self, mode):
        if self.busy: return
        port = self.port.get().split(' — ')[0]
        if port not in self.devices:
            messagebox.showerror('COM port', 'Select a valid COM port.'); return
        try:
            layout = partitions()
            app_off, app_size = layout['app0'][2:4]
            ota_off, ota_size = layout['otadata'][2:4]
            fw = BASE / 'firmware.bin' if mode == 'factory' else Path(self.firmware.get())
            if mode == 'factory':
                check_image(BASE / 'bootloader.bin')
            fw_len = check_image(fw)
            if fw_len > app_size:
                raise ValueError(f'Firmware is {fw_len:,} bytes, exceeding app0 size {app_size:,}.')
            if mode == 'factory' and not (BASE / 'partitions.bin').is_file():
                raise ValueError('Missing partitions.bin')
        except Exception as e:
            messagebox.showerror('Invalid firmware files', str(e)); return
        if mode == 'factory':
            msg = 'Factory installation ERASES ALL flash, including Wi-Fi, claim/device identity, calibration, settings and logs. This cannot be undone. Continue?'
        else:
            msg = 'This writes the new firmware to app0 and then clears OTA boot selection to boot app0. NVS and filesystem partitions remain untouched. Ensure this is compatible AIDoser ESP32-S3 firmware. Continue?'
        if not messagebox.askyesno('Confirm installation', msg, icon='warning'): return
        self.busy = True
        self.factory.config(state='disabled'); self.update.config(state='disabled')
        self.progress.start(12)
        self.status.set('Working — do not disconnect the programmer or power.')
        threading.Thread(target=self.run, args=(port,mode,fw,app_off,ota_off,ota_size), daemon=True).start()

    def command(self, port, *args):
        cmd = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', port, '--baud', '115200', *args]
        self.logline('> ' + ' '.join(cmd[3:]))
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1, errors='replace', creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        for line in p.stdout: self.logline(line.rstrip())
        rc = p.wait()
        if rc: raise RuntimeError(f'Programming failed (exit {rc}). Check COM port, wiring, BOOT/EN and power. See log above.')

    def run(self, port, mode, fw, app_off, ota_off, ota_size):
        try:
            self.command(port, 'read_mac')
            if mode == 'factory':
                self.command(port, 'erase_flash')
                self.command(port, 'write_flash', '-z', '--flash_mode', 'keep', '--flash_freq', 'keep', '--flash_size', '8MB',
                             '0x0', str(BASE/'bootloader.bin'), '0x8000', str(BASE/'partitions.bin'), hex(app_off), str(fw))
            else:
                # Write firmware FIRST; clear otadata only after successful write.
                self.command(port, 'write_flash', '-z', hex(app_off), str(fw))
                self.command(port, 'erase_region', hex(ota_off), hex(ota_size))
            self.q.put(('done', 'SUCCESS: Firmware installed. Reset the AIDoser to start it.'))
        except Exception as e:
            self.q.put(('error', str(e)))

    def poll(self):
        try:
            while True:
                kind, msg = self.q.get_nowait()
                if kind == 'log':
                    self.log.config(state='normal'); self.log.insert('end', msg+'\n'); self.log.see('end'); self.log.config(state='disabled')
                else:
                    self.busy = False; self.progress.stop()
                    self.factory.config(state='normal'); self.update.config(state='normal')
                    self.status.set(msg)
                    if kind == 'error': messagebox.showerror('Installation failed', msg)
                    else: messagebox.showinfo('Installation complete', msg)
        except queue.Empty: pass
        self.root.after(100, self.poll)

if __name__ == '__main__':
    root = tk.Tk(); Installer(root); root.mainloop()
