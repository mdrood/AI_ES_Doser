#include "apexapi.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include "logger.h"
#include <esp_task_wdt.h>
// Fixed 2026-10-09: every diagnostic in this file used to be Serial-only,
// so none of it ever reached an uploaded log zip -- there was no way to
// tell, after the fact, whether Apex/Trident actually returned a fresh
// Cond/Salt (-> SG/PPT) reading on a given poll, or whether it silently
// never matched any of the probe-name patterns below and SG/PPT just
// stayed blank forever. `logger` (declared extern in logger.h) mirrors
// these same lines into the durable log so a future "why is SG/PPT
// blank" question is answerable from one log pull instead of another
// multi-day field investigation.

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

  // Fixed 2026-08-13: this used to allocate a flat 96KB buffer fresh on
  // every single poll, regardless of how much of the real payload this
  // code actually reads. reefDoser12 polls Apex constantly (visible as
  // CHEMISTRY REPEAT POLL in the logs, happening far more often than
  // Trident actually produces a new real test) -- that's a large,
  // repeated heap allocation on a very hot path, a real and plausible
  // contributor to the heap pressure that caused reefDoser12's confirmed
  // OTA crash earlier tonight, and a likely factor in its repeated
  // reboots more generally (reefDoser1, which never calls this function
  // at all since it doesn't use Apex, has stayed reboot-free for 3+ days
  // on the identical firmware).
  //
  // A DeserializationOption::Filter tells the parser to only retain the
  // specific fields listed below -- everything else in the real payload
  // (however large) gets skipped during parsing rather than stored, so
  // memory use scales with what's actually kept, not with the incoming
  // payload's total size. The filter mirrors both JSON shapes this
  // function already handles (istat-wrapped and flat), since an unmatched
  // branch in the filter is simply ignored, not an error.
  StaticJsonDocument<256> filter;
  filter["istat"]["date"] = true;
  JsonObject filterIstatInput = filter["istat"]["inputs"].createNestedObject();
  filterIstatInput["did"] = true;
  filterIstatInput["type"] = true;
  filterIstatInput["name"] = true;
  filterIstatInput["value"] = true;
  filter["istat"]["outputs"].createNestedObject()["intensity"] = true;

  filter["system"]["date"] = true;
  JsonObject filterInput = filter["inputs"].createNestedObject();
  filterInput["did"] = true;
  filterInput["type"] = true;
  filterInput["name"] = true;
  filterInput["value"] = true;
  filter["outputs"].createNestedObject()["intensity"] = true;

  // 8KB is generous for the filtered fields across even a large multi-probe
  // Apex system -- a substantial reduction from the previous 96KB, since
  // only did/type/name/value per input and intensity per output are kept.
  DynamicJsonDocument doc(8 * 1024);

  DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
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
    // Alk match ranking (added 2026-10-27): an exact "alkx3" name beats a
    // type=="alk" match, which beats a loose name-contains-"alk" match.
    // Previously ANY input with "alk" in its name or type simply overwrote
    // out.alk, so a second Alk-ish input later in the list silently replaced
    // the real Trident Alk. Equal ranks still keep the last one seen, so a
    // system with a single candidate behaves exactly as before.
    int alkRank = 0;
    int alkCandidates = 0;
    String alkCandidateLog;

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
      else if (type == "ph" || did == "base_ph" || name.indexOf("pH14ft") >= 0) {
        if (did == "14_1" || name.indexOf("pH14ft") >= 0 || !isfinite(phFound)) phFound = v;
      }
      else if (type == "cond" || did == "base_cond" || name == "salt" || name.indexOf("cond") >= 0) {
        if (did == "base_cond" || name == "salt" || !isfinite(out.cond)) out.cond = v;
      }
      else if (type == "alk" || name.indexOf("alk") >= 0) {
        const int rank = (name == "alkx3") ? 3 : (type == "alk") ? 2 : 1;
        alkCandidates++;
        if (alkCandidateLog.length() < 160) {
          alkCandidateLog += did + "/" + type + "/" + name + "=" + String(v, 2) + " ";
        }
        if (rank >= alkRank) {
          out.alk = v;
          alkRank = rank;
        }
      }
      else if (type == "ca" || name.indexOf("cax3") >= 0) {
        // Fixed 2026-10-09: `name` is lowercased a few lines above, but this
        // compared it against "Cax3" (mixed case) -- indexOf() is
        // case-sensitive, so that half of the check could never match.
        // Harmless today only because Apex's JSON "type" field apparently
        // already says "ca" for this probe; this would silently fail
        // Ca-matching-by-name on any system where it doesn't.
        out.ca = v;
      }
      else if (type == "mg" || name.indexOf("mgx3") >= 0) {
        out.mg = v;
      }
    }
    out.tempF = tempFound;
    out.ph    = phFound;

    // One line per boot, and only when the feed really is ambiguous (more
    // than one Alk-like input), so a remote unit's log shows what the Apex
    // actually sent without adding any per-poll logging.
    static bool alkAmbiguityLogged = false;
    if (alkCandidates > 1 && !alkAmbiguityLogged) {
      alkAmbiguityLogged = true;
      logger.printf("APEX ALK CANDIDATES (%d): %s-> using %.2f\n",
                    alkCandidates, alkCandidateLog.c_str(), out.alk);
    }
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
// Fixed 2026-10-11: this runs on loopTask, which is registered with a 30s
// task watchdog (esp_task_wdt_init(30, true) in main.cpp). The old code used
// a 15s timeout per request AND always retried the other endpoint (JSON then
// XML), so one unreachable/slow Apex could block loop() for ~30s+ -- exactly
// the watchdog limit -- and reset the whole controller. On reefDoser12 nearly
// every TASK_WDT reset in the logs lands right after an Apex poll or the
// Firebase publish that follows it, and the three resets on 10-11 came about
// one poll interval apart. A LAN Apex answers in well under a second, so a
// short timeout loses nothing when it's healthy, and a failed transport (no
// connection / no response) skips the second endpoint since it would hang the
// same way.
static const uint16_t APEX_CONNECT_TIMEOUT_MS = 3000;
static const uint16_t APEX_READ_TIMEOUT_MS    = 4000;

// Backoff after failed polls so a dead Apex costs a few seconds per several
// minutes instead of blocking every poll cycle.
static uint8_t  apexConsecutiveFailures = 0;
static unsigned long apexNextPollAllowedMs = 0;
static unsigned long apexLastProblemLogMs = 0;

// ---------------------------------------------------------------------------
// P-3.0.28: overall time limit for one Apex fetch.
//
// Found on reefDoserT (mock Apex that answers slowly): HTTPClient's 4 s timeout
// restarts every time a few bytes arrive, so an Apex that trickles its answer
// keeps http.getString() waiting for as long as it likes. A 51 s answer froze
// loop() past the 30 s task watchdog and rebooted the controller (stage
// 'apex-poll' / syncAllTruths). The body is now read here with a hard overall
// deadline measured from the start of the fetch; on expiry the poll counts as
// a failed transport (existing back-off, no second endpoint attempt).
//
// The reader is a template over tiny Src/Clock adapters so the exact same text
// is host-tested against a real slow server (see test notes); on the device the
// adapters wrap HTTPClient and millis()/delay().
// ---------------------------------------------------------------------------
static const uint32_t APEX_TOTAL_DEADLINE_MS = 12000;   // whole fetch: connect + headers + body
static const size_t   APEX_MAX_BODY_BYTES    = 48 * 1024;

enum ApexBodyResult { APEX_BODY_OK = 0, APEX_BODY_TIMEOUT = 1, APEX_BODY_TOO_BIG = 2 };

//BEGIN_BOUNDED_READER
template <typename Src, typename Clock, typename Out>
static ApexBodyResult apexReadBodyBounded(Src& src, Clock& clk, long contentLen,
                                          uint32_t startedMs, uint32_t deadlineMs,
                                          uint32_t stallMs, Out& out) {
  uint8_t buf[256];
  uint32_t lastByteMs = clk.now();
  for (;;) {
    if (contentLen >= 0 && (long)out.length() >= contentLen) return APEX_BODY_OK;
    const size_t avail = src.available();
    if (avail > 0) {
      const size_t want = avail < sizeof(buf) ? avail : sizeof(buf);
      const size_t n = src.readInto(buf, want);
      if (n > 0) {
        out.concat((const char*)buf, (unsigned int)n);
        lastByteMs = clk.now();
        if (out.length() > APEX_MAX_BODY_BYTES) return APEX_BODY_TOO_BIG;
        continue;
      }
    } else if (!src.connected()) {
      return APEX_BODY_OK;               // server closed: the whole body has arrived
    }
    const uint32_t now = clk.now();
    if ((uint32_t)(now - startedMs) >= deadlineMs) return APEX_BODY_TIMEOUT;
    if ((uint32_t)(now - lastByteMs) >= stallMs) return APEX_BODY_TIMEOUT;
    clk.pause(2);
  }
}
//END_BOUNDED_READER

struct ApexHttpSrc {
  HTTPClient& http;
  WiFiClient* s;
  explicit ApexHttpSrc(HTTPClient& h) : http(h), s(h.getStreamPtr()) {}
  size_t available() { return s ? (size_t)s->available() : 0; }
  size_t readInto(uint8_t* b, size_t n) { return s ? s->readBytes(b, n) : 0; }
  bool connected() { return s && http.connected(); }
};
struct ApexClock {
  uint32_t now() { return millis(); }
  void pause(uint32_t ms) { delay(ms); }
};

static bool httpGetBody(const String& url, String& bodyOut, bool* transportFailed = nullptr) {
  bodyOut = "";
  if (transportFailed) *transportFailed = false;
  const uint32_t startedMs = millis();
  WiFiClient client;
  HTTPClient http;

  if (!http.begin(client, url)) {
    Serial.println("http.begin() failed");
    if (transportFailed) *transportFailed = true;
    return false;
  }

  http.setConnectTimeout(APEX_CONNECT_TIMEOUT_MS);
  http.setTimeout(APEX_READ_TIMEOUT_MS);
  static const char* wantHeaders[] = {"Transfer-Encoding"};   // HTTPClient::collectHeaders takes const char*[]
  http.collectHeaders(wantHeaders, 1);

  esp_task_wdt_reset();
  int httpCode = http.GET();
  esp_task_wdt_reset();
  if (httpCode <= 0) {
    Serial.printf("HTTP GET failed (%s): %s\n", url.c_str(), http.errorToString(httpCode).c_str());
    http.end();
    if (transportFailed) *transportFailed = true;
    return false;
  }

  if (httpCode != 200) {
    Serial.printf("HTTP GET %s -> %d\n", url.c_str(), httpCode);
    http.end();
    return false;
  }

  String te = http.header("Transfer-Encoding");
  te.toLowerCase();
  if (te.indexOf("chunked") >= 0) {
    // Chunked answers keep the previous behaviour (HTTPClient decodes them).
    bodyOut = http.getString();
    http.end();
    return bodyOut.length() > 0;
  }

  ApexHttpSrc src(http);
  ApexClock clk;
  const ApexBodyResult r = apexReadBodyBounded(src, clk, (long)http.getSize(), startedMs,
                                               APEX_TOTAL_DEADLINE_MS, APEX_READ_TIMEOUT_MS, bodyOut);
  http.end();

  if (r == APEX_BODY_TIMEOUT) {
    Serial.printf("Apex answer abandoned after %lu ms (limit %lu ms): %s\n",
                  (unsigned long)(millis() - startedMs), (unsigned long)APEX_TOTAL_DEADLINE_MS, url.c_str());
    bodyOut = "";
    if (transportFailed) *transportFailed = true;
    return false;
  }
  if (r == APEX_BODY_TOO_BIG) {
    Serial.printf("Apex answer larger than %u bytes; ignored: %s\n", (unsigned)APEX_MAX_BODY_BYTES, url.c_str());
    bodyOut = "";
    return false;
  }
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

  // Still backing off from recent failures -- skip this poll entirely
  // (returns "" like any other failed poll; caller already handles that).
  if (apexConsecutiveFailures > 0 && (long)(millis() - apexNextPollAllowedMs) < 0) {
    return "";
  }

  const unsigned long pollStartMs = millis();
  esp_task_wdt_reset();

  bool transportFailed = false;

  auto tryJson = [&](String& body) -> bool {
    transportFailed = false;
    if (!httpGetBody(urlJson, body, &transportFailed)) return false;
    bool ok = parseApexStatusJson(body, latest);
    if (!ok) {
      Serial.println("Apex JSON fetched but parse/values invalid");
      logger.println("Apex JSON fetched but parse/values invalid");
    }
    return ok;
  };

  auto tryXml = [&](String& body) -> bool {
    transportFailed = false;
    if (!httpGetBody(urlXml, body, &transportFailed)) return false;
    bool ok = parseApexLatestXml(body, latest);
    if (!ok) {
      Serial.println("Apex XML fetched but parse/values invalid");
      logger.println("Apex XML fetched but parse/values invalid");
    }
    return ok;
  };

  String body = "";
  bool ok = false;

  // Prefer based on flag, and fall back to the other endpoint -- but only
  // when the first one actually answered (e.g. 404 or unparseable). If the
  // connection itself failed, the other endpoint would hang the same way, so
  // don't spend another timeout on it.
  if (apexJson) {
    ok = tryJson(body);
    if (!ok && !transportFailed) ok = tryXml(body);
  } else {
    ok = tryXml(body);
    if (!ok && !transportFailed) ok = tryJson(body);
  }

  const unsigned long pollDurationMs = millis() - pollStartMs;
  esp_task_wdt_reset();

  // Rate-limited durable record (at most one line per 10 min): failures and
  // unusually slow polls were previously Serial-only, so none of this ever
  // reached an uploaded log.
  const bool canLogProblem = (apexLastProblemLogMs == 0) || (millis() - apexLastProblemLogMs >= 600000UL);
  if (!ok) {
    if (apexConsecutiveFailures < 250) apexConsecutiveFailures++;
    unsigned long backoffMs = 60000UL << (apexConsecutiveFailures > 4 ? 3 : (apexConsecutiveFailures - 1)); // 60s,120s,240s,480s
    if (backoffMs > 600000UL) backoffMs = 600000UL;
    apexNextPollAllowedMs = millis() + backoffMs;
    if (canLogProblem) {
      apexLastProblemLogMs = millis();
      logger.printf("APEX POLL FAILED #%u after %lums (transportFailed=%d); backing off %lus\n",
                    (unsigned)apexConsecutiveFailures, pollDurationMs, (int)transportFailed, backoffMs / 1000UL);
    }
  } else {
    if (apexConsecutiveFailures > 0) {
      logger.printf("APEX POLL RECOVERED after %u failed polls (took %lums)\n",
                    (unsigned)apexConsecutiveFailures, pollDurationMs);
      apexLastProblemLogMs = millis();
    } else if (pollDurationMs > 3000 && canLogProblem) {
      apexLastProblemLogMs = millis();
      logger.printf("APEX POLL SLOW: %lums\n", pollDurationMs);
    }
    apexConsecutiveFailures = 0;
    apexNextPollAllowedMs = 0;
  }

  if (ok) {
    Serial.print("Apex date: ");
    Serial.println(latest.date);
    Serial.printf("TempF=%.2f pH=%.2f Cond=%.2f Alk=%.2f Ca=%.1f Mg=%.0f\n",
                  latest.tempF, latest.ph, latest.cond, latest.alk, latest.ca, latest.mg);

    // Kept out of the Serial-only firehose on purpose: the user specifically
    // wants durable confirmation of Temp/Cond (Temp/SG missing on the
    // dashboard), without adding a new line to the logger on every single
    // poll. Only writes once to the durable log when Temp or Cond actually
    // CHANGES value, plus one heartbeat every ~60 polls (roughly hourly at
    // the normal ~5min poll cadence) so a long run of "still NAN" is still
    // visible without being a line-per-poll flood.
    static float lastLoggedTempF = NAN;
    static float lastLoggedCond = NAN;
    static uint16_t pollsSinceApexLog = 0;
    bool tempChanged = !isfinite(lastLoggedTempF) != !isfinite(latest.tempF) ||
                        (isfinite(latest.tempF) && fabs(latest.tempF - lastLoggedTempF) > 0.05f);
    bool condChanged = !isfinite(lastLoggedCond) != !isfinite(latest.cond) ||
                        (isfinite(latest.cond) && fabs(latest.cond - lastLoggedCond) > 0.05f);
    pollsSinceApexLog++;
    if (tempChanged || condChanged || pollsSinceApexLog >= 60) {
      logger.printf("APEX POLL: TempF=%.2f Cond=%.2f (pH=%.2f Alk=%.2f Ca=%.1f Mg=%.0f)\n",
                    latest.tempF, latest.cond, latest.ph, latest.alk, latest.ca, latest.mg);
      lastLoggedTempF = latest.tempF;
      lastLoggedCond = latest.cond;
      pollsSinceApexLog = 0;
    }

    setTempF(latest.tempF);
    setPh(latest.ph);
    setCond(latest.cond);
    setAlk(latest.alk);
    setCa(latest.ca);
    setMg(latest.mg);
    setDate(String(latest.date));

    Serial.printf("CLASS: TempF=%.2f pH=%.2f Cond=%.2f Alk=%.2f Ca=%.1f Mg=%.0f\n",
                  getTempF(), getPh(), getCond(), getAlk(), getCa(), getMg());
  } else {
    Serial.println("Failed to fetch/parse Apex via JSON and XML.");
    logger.println("Failed to fetch/parse Apex via JSON and XML.");
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
String ApexApi::getDate(){ return dateStr; }

void ApexApi::setTempF(float v){ tempF = v; }
void ApexApi::setPh(float v){ ph = v; }
void ApexApi::setCond(float v){ cond = v; }
void ApexApi::setAlk(float v){ alk = v; }
void ApexApi::setCa(float v){ ca = v; }
void ApexApi::setMg(float v){ mg = v; }
void ApexApi::setDate(String v){ dateStr = v; }