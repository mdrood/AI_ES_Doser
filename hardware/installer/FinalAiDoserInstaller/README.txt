AIDOSER WINDOWS FIRMWARE INSTALLER — ESP32-S3-WROOM-1-N8R8

This is a Windows double-click BAT launcher with a graphical installer, NOT a self-contained EXE.
It requires Python 3 and an internet connection for first-time installation of esptool.
It has not been tested on physical AIDoser hardware. Test on a bench board first.

1. Extract the ZIP completely into a folder.
2. Connect ESP-PROG-2 PROG connector to the ESP32-S3:
   programmer TXD -> GPIO44 (U0RXD), RXD -> GPIO43 (U0TXD),
   GND -> GND, IO0 -> GPIO0, EN -> EN.
   Power using a safe 3.3 V supply; do not connect two active 3.3 V supplies together.
3. Connect ESP-PROG-2 to the Windows PC with a USB data cable.
4. Double-click Install_AIDoser.bat. Select the correct COM port.
5. FACTORY INSTALL erases ALL flash, including Wi-Fi, identity, calibration and logs.
6. UPDATE FIRMWARE: Browse to a newly supplied firmware.bin and select Update.
   This writes app0 and clears OTA selection; it does not erase NVS or filesystem.
   The new image MUST target ESP32-S3, fit app0, and use a compatible partition layout.
   Existing device settings may still be incompatible with a changed firmware version.

FLASH LAYOUT FROM UPLOADED partitions.bin (8 MB):
  Bootloader: 0x000000
  Partition table: 0x008000
  NVS: 0x009000 size 0x005000
  OTA metadata: 0x00E000 size 0x002000
  app0: 0x010000 size 0x300000
  app1: 0x310000 size 0x300000
  SPIFFS: 0x610000 size 0x1F0000

NOTES:
- The update function is a wired recovery/update workflow, not an OTA update.
- If your device normally runs from app1, this installer switches it to app0.
- Factory erasing removes all device-specific provisioning, claim codes and calibration.
- The installer checks image chip ID and size, but cannot guarantee application compatibility.
- Do not flash older ESP32 firmware or use a different partition layout for updates.
