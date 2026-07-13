#include "Ota.h"

OtaManager::OtaManager() {}

String OtaManager::normalizeMac(String mac) {
    mac.trim();
    mac.toUpperCase();
    return mac;
}

String OtaManager::normalizeChipId(String chipId) {
    chipId.trim();
    chipId.toUpperCase();
    return chipId;
}

String OtaManager::getLocalMacAddress() {
    return normalizeMac(WiFi.macAddress());
}

String OtaManager::getLocalChipId() {
    uint64_t chipid = ESP.getEfuseMac();
    char buf[13];
    snprintf(buf, sizeof(buf), "%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
    return String(buf);
}

String OtaManager::absoluteUrlFromManifestUrl(const String& manifestUrl, const String& maybeRelativeUrl) {
    String fw = maybeRelativeUrl;
    fw.trim();

    if (fw.startsWith("http://") || fw.startsWith("https://")) {
        return fw;
    }

    int schemeEnd = manifestUrl.indexOf("://");
    if (schemeEnd < 0) {
        return fw;
    }

    int hostStart = schemeEnd + 3;
    int pathStart = manifestUrl.indexOf('/', hostStart);
    if (pathStart < 0) {
        return manifestUrl + "/" + fw;
    }

    String origin = manifestUrl.substring(0, pathStart);

    if (fw.startsWith("/")) {
        return origin + fw;
    }

    int lastSlash = manifestUrl.lastIndexOf('/');
    if (lastSlash < pathStart) {
        return origin + "/" + fw;
    }

    return manifestUrl.substring(0, lastSlash + 1) + fw;
}

String OtaManager::manifestUrlFromInputUrl(String url) {
    url.trim();

    if (url.endsWith(".json")) {
        return url;
    }

    if (url.endsWith(".bin")) {
        String jsonUrl = url;
        jsonUrl.remove(jsonUrl.length() - 4);
        jsonUrl += ".json";
        return jsonUrl;
    }

    return url;
}

bool OtaManager::downloadManifest(const String& manifestUrl, JsonDocument& doc) {
    Serial.printf("OTA manifest URL: %s\n", manifestUrl.c_str());

    HTTPClient http;
    http.begin(manifestUrl);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("OTA manifest HTTP error: %s\n", http.errorToString(httpCode).c_str());
        http.end();
        return false;
    }

    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("OTA manifest JSON parse failed: %s\n", err.c_str());
        return false;
    }

    return true;
}

bool OtaManager::validateManifest(JsonDocument& doc,
                                  const String& expectedDeviceId,
                                  const String& expectedMac,
                                  const String& expectedChipId,
                                  String& firmwareUrl,
                                  const String& manifestUrl) {
    String manifestDeviceId = doc["deviceId"] | "";
    String manifestMac = doc["macAddress"] | "";
    if (manifestMac.length() == 0) manifestMac = doc["mac"] | "";
    String manifestChipId = doc["chipId"] | "";

    String manifestFirmware = doc["firmware"] | "";
    if (manifestFirmware.length() == 0) manifestFirmware = doc["firmwareUrl"] | "";
    if (manifestFirmware.length() == 0) manifestFirmware = doc["url"] | "";
    if (manifestFirmware.length() == 0) manifestFirmware = doc["binUrl"] | "";

    String localMac = getLocalMacAddress();
    String localChipId = getLocalChipId();

    manifestDeviceId.trim();
    manifestMac = normalizeMac(manifestMac);
    manifestChipId = normalizeChipId(manifestChipId);

    String expectedMacNorm = normalizeMac(expectedMac);
    String expectedChipNorm = normalizeChipId(expectedChipId);

    Serial.println("========================================");
    Serial.println("OTA SAFETY CHECK");
    Serial.println("========================================");
    Serial.printf("Running deviceId : %s\n", expectedDeviceId.c_str());
    Serial.printf("Manifest deviceId: %s\n", manifestDeviceId.c_str());
    Serial.printf("Local MAC        : %s\n", localMac.c_str());
    Serial.printf("Expected MAC     : %s\n", expectedMacNorm.c_str());
    Serial.printf("Manifest MAC     : %s\n", manifestMac.c_str());
    Serial.println("Chip ID check    : disabled; MAC safety only");

    if (expectedDeviceId.length() == 0 || manifestDeviceId.length() == 0 || manifestDeviceId != expectedDeviceId) {
        Serial.println("OTA BLOCKED: deviceId mismatch.");
        return false;
    }

    if (expectedMacNorm.length() == 0 || manifestMac.length() == 0 || localMac != expectedMacNorm || manifestMac != expectedMacNorm) {
        Serial.println("OTA BLOCKED: MAC address mismatch.");
        return false;
    }


    if (manifestFirmware.length() == 0) {
        Serial.println("OTA BLOCKED: manifest has no firmware URL.");
        return false;
    }

    firmwareUrl = absoluteUrlFromManifestUrl(manifestUrl, manifestFirmware);

    if (firmwareUrl.indexOf("/devices/" + expectedDeviceId + "/") < 0) {
        Serial.println("OTA BLOCKED: manifest firmware URL is not inside this device folder.");
        Serial.print("Firmware URL: ");
        Serial.println(firmwareUrl);
        return false;
    }

    Serial.println("OTA SAFETY CHECK PASSED.");
    Serial.println("========================================");
    return true;
}

void OtaManager::updateFirmware(String url) {
    updateFirmware(url, "", "", "");
}

void OtaManager::updateFirmware(String url,
                                const String& expectedDeviceId,
                                const String& expectedMac,
                                const String& expectedChipId) {
    if (url.length() == 0) return;

    String manifestUrl = manifestUrlFromInputUrl(url);
    JsonDocument doc;
    String firmwareUrl;

    if (!downloadManifest(manifestUrl, doc)) {
        Serial.println("OTA BLOCKED: could not download or parse manifest JSON.");
        return;
    }

    if (!validateManifest(doc, expectedDeviceId, expectedMac, expectedChipId, firmwareUrl, manifestUrl)) {
        return;
    }

    Serial.printf("Starting OTA Update from: %s\n", firmwareUrl.c_str());

    HTTPClient http;
    http.begin(firmwareUrl);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        if (contentLength <= 0) {
            Serial.println("OTA BLOCKED: invalid firmware content length.");
            http.end();
            return;
        }

        bool canBegin = Update.begin(contentLength);

        if (canBegin) {
            Serial.println("Begin OTA. This may take a minute...");
            WiFiClient* client = http.getStreamPtr();
            size_t written = Update.writeStream(*client);

            if (written == (size_t)contentLength) {
                Serial.println("Written : " + String(written) + " successfully");
            } else {
                Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
            }

            if (Update.end()) {
                Serial.println("OTA done!");
                if (Update.isFinished()) {
                    Serial.println("Update successfully completed. Rebooting...");
                    ESP.restart();
                } else {
                    Serial.println("Update not finished? Something went wrong.");
                }
            } else {
                Serial.println("Error Occurred. Error #: " + String(Update.getError()));
            }
        } else {
            Serial.println("Not enough space to begin OTA");
        }
    } else {
        Serial.printf("Firmware HTTP error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
}