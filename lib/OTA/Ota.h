#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

class OtaManager {
public:
    OtaManager();
    // Path to the .bin file on your Firebase hosting (e.g., https://aidoser.web.app/firmware.bin)
    void updateFirmware(String url);

private:
    bool validateUrl(String url);
};

#endif