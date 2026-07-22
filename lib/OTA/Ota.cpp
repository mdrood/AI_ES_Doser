#include "Ota.h"
#include <esp_task_wdt.h>

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
    if (url.length() == 0) {
        Serial.println("OTA BLOCKED: empty input URL.");
        return;
    }

    String manifestUrl = manifestUrlFromInputUrl(url);
    JsonDocument doc;
    String firmwareUrl;

    Serial.println("========================================");
    Serial.println("OTA TRANSFER START");
    Serial.printf("OTA input URL   : %s\n", url.c_str());
    Serial.printf("OTA manifest URL: %s\n", manifestUrl.c_str());
    Serial.printf("OTA free heap   : %u\n", ESP.getFreeHeap());
    Serial.printf("OTA free sketch : %u\n", ESP.getFreeSketchSpace());
    Serial.println("========================================");

    if (!downloadManifest(manifestUrl, doc)) {
        Serial.println("OTA BLOCKED: could not download or parse manifest JSON.");
        return;
    }

    if (!validateManifest(
            doc,
            expectedDeviceId,
            expectedMac,
            expectedChipId,
            firmwareUrl,
            manifestUrl)) {
        return;
    }

    Serial.printf("Starting OTA Update from: %s\n", firmwareUrl.c_str());

    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(15000);

    if (!http.begin(firmwareUrl)) {
        Serial.println("OTA FAILED: HTTP begin failed.");
        return;
    }

    int httpCode = http.GET();
    Serial.printf("OTA firmware HTTP status: %d\n", httpCode);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf(
            "OTA FAILED: firmware HTTP error: %s\n",
            http.errorToString(httpCode).c_str()
        );
        http.end();
        return;
    }

    int contentLength = http.getSize();
    Serial.printf("OTA content length: %d bytes\n", contentLength);
    Serial.printf("OTA available space: %u bytes\n", ESP.getFreeSketchSpace());

    if (contentLength <= 0) {
        Serial.println("OTA FAILED: invalid or missing firmware content length.");
        http.end();
        return;
    }

    if (!Update.begin((size_t)contentLength, U_FLASH)) {
        Serial.printf(
            "OTA FAILED: Update.begin error=%u (%s)\n",
            Update.getError(),
            Update.errorString()
        );
        http.end();
        return;
    }

    Serial.println("OTA Update.begin succeeded.");
    Serial.println("OTA downloading and writing firmware...");

    WiFiClient* stream = http.getStreamPtr();
    if (stream == nullptr) {
        Serial.println("OTA FAILED: HTTP stream pointer is null.");
        Update.abort();
        http.end();
        return;
    }

    static constexpr size_t OTA_BUFFER_SIZE = 4096;
    uint8_t buffer[OTA_BUFFER_SIZE];
    size_t totalWritten = 0;
    unsigned long lastDataMs = millis();
    unsigned long lastProgressMs = 0;

    while (http.connected() && totalWritten < (size_t)contentLength) {
        size_t available = stream->available();

        if (available > 0) {
            size_t remaining = (size_t)contentLength - totalWritten;
            size_t toRead = available;
            if (toRead > OTA_BUFFER_SIZE) toRead = OTA_BUFFER_SIZE;
            if (toRead > remaining) toRead = remaining;

            int bytesRead = stream->readBytes(buffer, toRead);
            if (bytesRead <= 0) {
                Serial.println("OTA FAILED: stream returned no data.");
                Update.abort();
                http.end();
                return;
            }

            size_t bytesWritten = Update.write(buffer, (size_t)bytesRead);
            if (bytesWritten != (size_t)bytesRead) {
                Serial.printf(
                    "OTA FAILED: flash write mismatch read=%d wrote=%u error=%u (%s)\n",
                    bytesRead,
                    (unsigned)bytesWritten,
                    Update.getError(),
                    Update.errorString()
                );
                Update.abort();
                http.end();
                return;
            }

            totalWritten += bytesWritten;
            lastDataMs = millis();

            if (lastProgressMs == 0 ||
                millis() - lastProgressMs >= 2000UL ||
                totalWritten == (size_t)contentLength) {

                float percent =
                    (100.0f * (float)totalWritten) / (float)contentLength;

                Serial.printf(
                    "OTA progress: %u/%d bytes (%.1f%%) heap=%u\n",
                    (unsigned)totalWritten,
                    contentLength,
                    percent,
                    ESP.getFreeHeap()
                );

                lastProgressMs = millis();
            }
        } else {
            if (millis() - lastDataMs > 15000UL) {
                Serial.printf(
                    "OTA FAILED: download stalled at %u/%d bytes.\n",
                    (unsigned)totalWritten,
                    contentLength
                );
                Update.abort();
                http.end();
                return;
            }

            delay(1);
        }

        // Keep the Arduino loop task and watchdog healthy during the blocking OTA.
        esp_task_wdt_reset();
        yield();
    }

    http.end();

    Serial.printf(
        "OTA download finished: wrote %u/%d bytes.\n",
        (unsigned)totalWritten,
        contentLength
    );

    if (totalWritten != (size_t)contentLength) {
        Serial.println("OTA FAILED: downloaded byte count did not match content length.");
        Update.abort();
        return;
    }

    if (!Update.end(true)) {
        Serial.printf(
            "OTA FAILED: Update.end error=%u (%s)\n",
            Update.getError(),
            Update.errorString()
        );
        return;
    }

    if (!Update.isFinished()) {
        Serial.println("OTA FAILED: Update.end succeeded but image is not marked finished.");
        return;
    }

    Serial.println("OTA SUCCESS: firmware fully written and verified.");
    Serial.println("OTA SUCCESS: rebooting into the new firmware...");
    Serial.flush();
    delay(500);
    ESP.restart();
}