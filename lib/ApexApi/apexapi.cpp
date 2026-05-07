#include "apexapi.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

// ------------------------------
// Shared readings struct (yours)
// ------------------------------
struct ApexLatest {
  char date[32];    // xml date string or json epoch string
  float tempF = NAN;
  float ph    = NAN;
  float alk   = NAN;
  float ca    = NAN;
  float cond  = NAN;   // salt/cond reading from Apex
  float mg    = NAN;
  int maxLightIntensity = 0;
};

static ApexLatest latest;

// ------------------------------
// ctor / config
// ------------------------------
ApexApi::ApexApi() {}

void ApexApi::setIpAddr(String ip) {
  apexIp = ip;
}

// =====================================================
// XML PARSER (your existing code, unchanged)
// =====================================================

bool extractTagValue(const String& src, const String& openTag, const String& closeTag, int from, String& out) {
  int a = src.indexOf(openTag, from);
  if (a < 0) return false;
  a += openTag.length();
  int b = src.indexOf(closeTag, a);
  if (b < 0) return false;
  out = src.substring(a, b);
  out.trim();
  return true;
}

bool getLastRecordBlock(const String& xml, String& recordOut) {
  int start = xml.lastIndexOf("<record>");
  if (start < 0) return false;
  int end = xml.indexOf("</record>", start);
  if (end < 0) return false;
  end += String("</record>").length();
  recordOut = xml.substring(start, end);
  return true;
}

// Finds <probe> ... <name>PROBE_NAME</name> ... <value>XYZ</value> ... </probe>
bool getProbeValueByName(const String& record, const String& probeName, float& outVal) {
  int pos = 0;
  while (true) {
    int p0 = record.indexOf("<probe>", pos);
    if (p0 < 0) return false;
    int p1 = record.indexOf("</probe>", p0);
    if (p1 < 0) return false;

    String probeBlock = record.substring(p0, p1);
    String name;
    if (extractTagValue(probeBlock, "<name>", "</name>", 0, name)) {
      if (name == probeName) {
        String valStr;
        if (!extractTagValue(probeBlock, "<value>", "</value>", 0, valStr)) return false;
        outVal = valStr.toFloat();
        return true;
      }
    }
    pos = p1 + 8; // len("</probe>")
  }
}

static bool parseApexLatestXml(const String& xml, ApexLatest& out) {
  String record;
  if (!getLastRecordBlock(xml, record)) return false;

  String date;
if (extractTagValue(record, "<date>", "</date>", 0, date)) {
    strncpy(out.date, date.c_str(), sizeof(out.date) - 1);
    out.date[sizeof(out.date) - 1] = '\0'; 
}
  // Use YOUR probe names
  getProbeValueByName(record, "Tmp",    out.tempF);
  getProbeValueByName(record, "SumppH", out.ph);
  getProbeValueByName(record, "Salt",   out.cond);
  getProbeValueByName(record, "Alkx3",  out.alk);
  getProbeValueByName(record, "Cax3",   out.ca);
  getProbeValueByName(record, "Mgx3",   out.mg);

  return true;
}

// =====================================================
// JSON PARSER for /cgi-bin/status.json
// Supports BOTH shapes:
//  A) { "istat": { "date":..., "inputs":[...] } }
//  B) { "system":{ "date":... }, "inputs":[...], ... }   <-- your REST payload
// =====================================================
static bool parseApexStatusJson(const String& payload, ApexLatest& out) {
  if (payload.length() < 10) return false;
  out = ApexLatest();

  // Increase the filter/size for large Apex deployments
  // For your 14-foot reef, 96KB is a safe bet.
  DynamicJsonDocument doc(96 * 1024); 
  
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("APEX JSON parse error: %s\n", err.c_str());
    // If it still says IncompleteInput, the String 'payload' itself 
    // was likely cut off during the HTTP GET.
    return false;
  }
  // Define variables once to avoid "redeclaration" errors
  JsonArray inputs;
  JsonArray outputs;
  String dateStr;
  JsonVariant istat = doc["istat"]; // Declare istat ONCE

  // 1. Identify Data Structure (Standard vs REST)
  if (!istat.isNull()) {
    if (!istat["date"].isNull()) dateStr = String((long)istat["date"]);
    inputs = istat["inputs"].as<JsonArray>();
    outputs = istat["outputs"].as<JsonArray>();
  } else {
    JsonVariant sys = doc["system"];
    if (!sys.isNull() && !sys["date"].isNull()) dateStr = String((long)sys["date"]);
    inputs = doc["inputs"].as<JsonArray>();
    outputs = doc["outputs"].as<JsonArray>();
  }

 if (dateStr.length()) {
    strncpy(out.date, dateStr.c_str(), sizeof(out.date) - 1);
    out.date[sizeof(out.date) - 1] = '\0';
}

  // 2. Parse Probes (Inputs)
  if (!inputs.isNull()) {
    float tempFound = NAN;
    float phFound   = NAN;

    for (JsonObject it : inputs) {
      const char* didC  = it["did"]  | "";
      const char* typeC = it["type"] | "";
      const char* nameC = it["name"] | "";
      float v = it["value"].isNull() ? NAN : (float)it["value"];
      if (!isfinite(v)) continue;

      String did(didC);
      String type(typeC);
      String name(nameC);
      did.toLowerCase();
      type.toLowerCase();
      name.toLowerCase();

      // Apex JSON identifies probes mostly by did + friendly name:
      //   did=base_Temp, name=Tmp
      //   did=base_pH,   name=SumppH
      //   did=base_Cond, name=Salt
      // The old parser checked base_Cond against name, so Salt/Cond could be missed.
      if (type == "temp" || did == "base_temp" || name == "tmp" || name.indexOf("temp") >= 0) {
        if (did == "base_temp" || name == "tmp" || !isfinite(tempFound)) tempFound = v;
      }
      else if (type == "ph" || did == "base_ph" || name.indexOf("ph") >= 0) {
        if (did == "base_ph" || name.indexOf("sump") >= 0 || !isfinite(phFound)) phFound = v;
      }
      else if (type == "cond" || did == "base_cond" || name == "salt" || name.indexOf("cond") >= 0) {
        if (did == "base_cond" || name == "salt" || !isfinite(out.cond)) out.cond = v;
      }
      else if (type == "alk" || name.indexOf("alk") >= 0) {
        out.alk = v;
      }
      else if (type == "ca" || name.indexOf("Cax3") >= 0) {
        out.ca = v;
      }
      else if (type == "mg" || name.indexOf("Mgx3") >= 0) {
        out.mg = v;
      }
    }
    out.tempF = tempFound;
    out.ph    = phFound;
  }

  // 3. Parse Light Intensity (Outputs - Name Agnostic)
  int highIntensity = 0;
  if (!outputs.isNull()) {
    for (JsonObject it : outputs) {
      // Look for the "intensity" key specifically, regardless of outlet name
      if (it.containsKey("intensity")) {
        int val = it["intensity"] | 0;
        if (val > highIntensity) highIntensity = val;
      }
    }
  }
  out.maxLightIntensity = highIntensity;

  // Return true if we got ANY useful data
  return (isfinite(out.tempF) || isfinite(out.ph) || isfinite(out.cond) || isfinite(out.alk) || isfinite(out.ca) || isfinite(out.mg) || out.maxLightIntensity > 0);
}

// =====================================================
// HTTP helper
// =====================================================
static bool httpGetBody(const String& url, String& bodyOut) {
  bodyOut = "";
  WiFiClient client;
  HTTPClient http;

  if (!http.begin(client, url)) {
    Serial.println("http.begin() failed");
    return false;
  }

  // keep it snappy so fallback doesn’t stall your loop
  http.setTimeout(15000);

  int httpCode = http.GET();
  if (httpCode <= 0) {
    Serial.printf("HTTP GET failed (%s): %s\n", url.c_str(), http.errorToString(httpCode).c_str());
    http.end();
    return false;
  }

  if (httpCode != 200) {
    Serial.printf("HTTP GET %s -> %d\n", url.c_str(), httpCode);
    http.end();
    return false;
  }

  bodyOut = http.getString();
  http.end();
  return bodyOut.length() > 0;
}

// =====================================================
// GET STATE with fallback:
// - If apexJson==true:  try JSON first, then XML
// - If apexJson==false: try XML first, then JSON
// =====================================================
String ApexApi::getState() {
  if (apexIp.length() == 0) {
    //TODO maybe put back in or tell dashboard
    Serial.println("ApexApi: apexIp not set");
    return "";
  }

  const String urlJson = "http://" + apexIp + "/cgi-bin/status.json";
  const String urlXml  = "http://" + apexIp + "/cgi-bin/status.xml";

  auto tryJson = [&](String& body) -> bool {
    if (!httpGetBody(urlJson, body)) return false;
    bool ok = parseApexStatusJson(body, latest);
    if (!ok) Serial.println("Apex JSON fetched but parse/values invalid");
    return ok;
  };

  auto tryXml = [&](String& body) -> bool {
    if (!httpGetBody(urlXml, body)) return false;
    bool ok = parseApexLatestXml(body, latest);
    if (!ok) Serial.println("Apex XML fetched but parse/values invalid");
    return ok;
  };

  String body = "";
  bool ok = false;

  // Prefer based on flag, but ALWAYS fallback to the other.
  if (apexJson) {
    ok = tryJson(body);
    if (!ok) ok = tryXml(body);
  } else {
    ok = tryXml(body);
    if (!ok) ok = tryJson(body);
  }

  if (ok) {
    Serial.print("Apex date: ");
    Serial.println(latest.date);
    Serial.printf("TempF=%.2f pH=%.2f Cond=%.2f Alk=%.2f Ca=%.1f Mg=%.0f\n",
                  latest.tempF, latest.ph, latest.cond, latest.alk, latest.ca, latest.mg);

    setTempF(latest.tempF);
    setPh(latest.ph);
    setCond(latest.cond);
    setAlk(latest.alk);
    setCa(latest.ca);
    setMg(latest.mg);

    Serial.printf("CLASS: TempF=%.2f pH=%.2f Cond=%.2f Alk=%.2f Ca=%.1f Mg=%.0f\n",
                  getTempF(), getPh(), getCond(), getAlk(), getCa(), getMg());
  } else {
    Serial.println("Failed to fetch/parse Apex via JSON and XML.");
    body = "";
  }

  return body;
}

// =====================================================
// getters / setters (your fixed ones)
// =====================================================
float ApexApi::getTempF(){ 
  
if (tempF < 60.0) {
    tempF = (tempF * 9.0 / 5.0) + 32.0;
}
  
  return tempF; 
}

bool ApexApi::getLightStatus() {
  // If the highest intensity found in the last pull is > 5, return true
  return latest.maxLightIntensity > 5;
}

float ApexApi::getPh(){ return ph; }
float ApexApi::getCond(){ return cond; }
float ApexApi::getAlk(){ return alk; }
float ApexApi::getCa(){ return ca; }
float ApexApi::getMg(){ return mg; }

void ApexApi::setTempF(float v){ tempF = v; }
void ApexApi::setPh(float v){ ph = v; }
void ApexApi::setCond(float v){ cond = v; }
void ApexApi::setAlk(float v){ alk = v; }
void ApexApi::setCa(float v){ ca = v; }
void ApexApi::setMg(float v){ mg = v; }