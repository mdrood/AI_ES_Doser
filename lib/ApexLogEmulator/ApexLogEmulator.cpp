#include "ApexLogEmulator.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

ApexLogEmulator::ApexLogEmulator() {}

void ApexLogEmulator::begin(const String& scriptUrl, unsigned long pollMs) {
  _scriptUrl = scriptUrl;
  _pollIntervalMs = pollMs;
  _lastPollMs = 0;
  _lastGoodMs = 0;
  _chem = ApexEmuChemistry();
}

bool ApexLogEmulator::loop() {
  if (_scriptUrl.length() == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  const unsigned long now = millis();
  if (_lastPollMs != 0 && (now - _lastPollMs) < _pollIntervalMs) return false;

  return pollNow();
}

bool ApexLogEmulator::pollNow() {
  _lastPollMs = millis();

  if (_scriptUrl.length() == 0) {
    _chem.valid = false;
    _chem.error = "missing script url";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    _chem.valid = false;
    _chem.error = "wifi not connected";
    return false;
  }

  // Google Apps Script uses HTTPS and often redirects. ESP32 needs a secure client
  // and redirect following or it may receive empty/null bytes instead of JSON.
  WiFiClientSecure client;
  client.setInsecure();  // Test-only emulator: accept Google TLS cert without storing CA.
  client.setTimeout(15000);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(30000);
  http.setReuse(false);
  http.addHeader("Accept", "application/json");
  http.addHeader("Cache-Control", "no-cache");
  http.addHeader("User-Agent", "reefDoser3-apex-emulator");

  String url = _scriptUrl;
  // Cache-bust so Apps Script/Google does not hand back an odd cached/empty response.
  url += (url.indexOf('?') >= 0) ? "&" : "?";
  url += "esp32=1&t=" + String(millis());

  Serial.println();
Serial.println("SCRIPT URL:");
Serial.println(_scriptUrl);
Serial.println();

  if (!http.begin(client, url)) {
    _chem.valid = false;
    _chem.error = "https begin failed";
    return false;
  }

  int code = http.GET();
  String body = http.getString();
  http.end();

  Serial.printf("APEX EMU HTTP code=%d bodyLen=%d\n", code, body.length());

  if (code != 200) {
    _chem.valid = false;
    _chem.error = "http " + String(code) + ": " + body.substring(0, 160);
    Serial.println("APEX EMU HTTP failed body preview:");
    Serial.println(body.substring(0, 300));
    return false;
  }

  // Remove any leading null/control garbage before JSON and any trailing junk after JSON.
  int firstBrace = body.indexOf('{');
  int lastBrace = body.lastIndexOf('}');
  if (firstBrace < 0 || lastBrace <= firstBrace) {
    _chem.valid = false;
    _chem.error = "no json object in response, len=" + String(body.length());
    Serial.println("APEX EMU non-JSON response preview:");
    Serial.println(body.substring(0, 300));
    return false;
  }

  String json = body.substring(firstBrace, lastBrace + 1);

  Serial.println();
  Serial.println("========================================");
  Serial.println("APEX EMU JSON RESPONSE");
  Serial.println("========================================");
  Serial.println(json.substring(0, 700));
  Serial.println("========================================");

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    _chem.valid = false;
    _chem.error = String("json parse failed: ") + err.c_str();
    Serial.print("APEX EMU parse failed. JSON preview: ");
    Serial.println(json.substring(0, 300));
    return false;
  }

  bool ok = doc["ok"] | false;
  if (!ok) {
    _chem.valid = false;
    _chem.error = doc["error"] | "apps script returned ok=false";
    return false;
  }

  ApexEmuChemistry next;
  next.valid = true;
  next.alk = doc["alk"] | NAN;
  next.ca = doc["ca"] | NAN;
  next.mg = doc["mg"] | NAN;
  next.ph = doc["ph"] | NAN;
  next.source = doc["source"] | "apex-log-emulator";
  next.deviceId = doc["deviceId"] | "";
  next.logTime = doc["logTime"] | "";

  // Require alk and pH minimum. Ca/Mg may be missing if the parser only found a STATS line.
  if (isnan(next.alk) || isnan(next.ph)) {
    _chem.valid = false;
    _chem.error = "missing alk or ph in response";
    return false;
  }

  _chem = next;
  _lastGoodMs = millis();

  Serial.printf("APEX EMU OK: Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f source=%s logTime=%s\n",
                _chem.alk, _chem.ca, _chem.mg, _chem.ph,
                _chem.source.c_str(), _chem.logTime.c_str());

  return true;
}

const ApexEmuChemistry& ApexLogEmulator::chemistry() const {
  return _chem;
}

bool ApexLogEmulator::hasValidChemistry() const {
  return _chem.valid;
}

unsigned long ApexLogEmulator::lastPollMs() const {
  return _lastPollMs;
}

unsigned long ApexLogEmulator::lastGoodMs() const {
  return _lastGoodMs;
}
