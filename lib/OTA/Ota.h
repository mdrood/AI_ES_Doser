#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_system.h>

class OtaManager {
public:
    OtaManager();

    // Backward-compatible wrapper. Prefer the safer overload below.
    void updateFirmware(String url);

    // Safe OTA: downloads the JSON manifest first and blocks flashing unless
    // deviceId, MAC address, and Chip ID all match this ESP32/build.
    void updateFirmware(String url,
                        const String& expectedDeviceId,
                        const String& expectedMac,
                        const String& expectedChipId);

private:
    String normalizeMac(String mac);
    String normalizeChipId(String chipId);
    String getLocalMacAddress();
    String getLocalChipId();
    String manifestUrlFromInputUrl(String url);
    String absoluteUrlFromManifestUrl(const String& manifestUrl, const String& maybeRelativeUrl);
    bool downloadManifest(const String& manifestUrl, JsonDocument& doc);
    bool validateManifest(JsonDocument& doc,
                          const String& expectedDeviceId,
                          const String& expectedMac,
                          const String& expectedChipId,
                          String& firmwareUrl,
                          const String& manifestUrl);
};

#endif
