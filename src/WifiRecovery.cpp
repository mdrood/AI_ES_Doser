#include "WifiRecovery.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>

#include "logger.h"

extern WebServer server;   // the dashboard server defined in main.cpp
extern String deviceID;    // defined in main.cpp

// ----- tunables -------------------------------------------------------------
#ifndef WIFI_RECOVERY_AFTER_MS
#define WIFI_RECOVERY_AFTER_MS 120000UL      // 2 minutes of normal reconnecting first
#endif
#ifndef WIFI_RECOVERY_RETRY_MS
#define WIFI_RECOVERY_RETRY_MS 30000UL       // background retry, hotspot closed
#endif
#ifndef WIFI_RECOVERY_RETRY_AP_MS
#define WIFI_RECOVERY_RETRY_AP_MS 60000UL    // background retry, hotspot open, nobody connected
#endif
#ifndef WIFI_RECOVERY_PENDING_MS
#define WIFI_RECOVERY_PENDING_MS 40000UL     // how long a newly typed password gets to connect
#endif
#ifndef WIFI_RECOVERY_STABLE_MS
#define WIFI_RECOVERY_STABLE_MS 10000UL      // connected this long = "really back"
#endif
#ifndef WIFI_RECOVERY_AP_PASSWORD
#define WIFI_RECOVERY_AP_PASSWORD ""         // open, same as the first-time setup hotspot; >=8 chars to protect it
#endif
// The hotspot must NOT sit on the same subnet as the customer's router
// (192.168.4.x is common, and is the ESP32 default hotspot subnet).
static const IPAddress AP_IP(192, 168, 73, 1);
static const IPAddress AP_GW(192, 168, 73, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

// ----- state ----------------------------------------------------------------
static bool          s_begun = false;
static bool          s_hasSaved = false;       // saved credentials exist in NVS
static String        s_savedSsid;
static String        s_savedPass;

static bool          s_wasConnected = false;
static unsigned long s_connectedSinceMs = 0;
static bool          s_lost = false;
static unsigned long s_lostSinceMs = 0;

static bool          s_hotspotActive = false;
static bool          s_manualRequested = false;  // P-3.0.26: status-button request
static uint32_t      s_manualMinutes = 0;
static unsigned long s_manualUntilMs = 0;       // 0 = hotspot was not opened by the button
static DNSServer     s_dns;
static bool          s_staPaused = false;       // retries paused while a phone is on the hotspot
static unsigned long s_lastClientMs = 0;

static unsigned long s_lastRetryMs = 0;
static unsigned long s_retryCount = 0;

static bool          s_pendingActive = false;
static String        s_pendSsid;
static String        s_pendPass;
static unsigned long s_pendingStartMs = 0;
static bool          s_justConnectedViaPortal = false;
static String        s_lastResult;              // shown on the setup page

// ----- helpers --------------------------------------------------------------
static void loadSaved() {
  Preferences p;
  p.begin("doser-settings", true);
  s_savedSsid = p.getString("ssid", "");
  s_savedPass = p.getString("pass", "");
  p.end();
  s_hasSaved = (s_savedSsid.length() > 0);
}

static void saveCredsOnce(const String& ssid, const String& pass) {
  Preferences p;
  p.begin("doser-settings", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
  s_savedSsid = ssid;
  s_savedPass = pass;
  s_hasSaved = true;
}

static String htmlEsc(const String& in) {
  String o;
  o.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else if (c == '\'') o += "&#39;";
    else o += c;
  }
  return o;
}

// Only answer requests that arrived through the hotspot itself. Anyone on the
// home network must not be able to change the WiFi credentials this way.
static bool requestIsFromHotspot() {
  return s_hotspotActive && server.client().localIP() == WiFi.softAPIP();
}

static void startHotspot(bool manual = false) {
  if (ESP.getMaxAllocHeap() < 30000) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      Serial.println("WIFI RECOVERY: hotspot NOT started (low memory); will retry later.");
      logger.println("WIFI RECOVERY: hotspot NOT started (low memory); will retry later.");
    }
    return;
  }
  WiFi.mode(WIFI_AP_STA);                    // keep the station side alive
  WiFi.softAPConfig(AP_IP, AP_GW, AP_MASK);
  String apName = "AIDoser-" + deviceID;
  String apPass = String(WIFI_RECOVERY_AP_PASSWORD);
  bool ok;
  if (apPass.length() >= 8) ok = WiFi.softAP(apName.c_str(), apPass.c_str(), 1, 0, 2);
  else                      ok = WiFi.softAP(apName.c_str(), nullptr, 1, 0, 2);
  if (!ok) {
    Serial.println("WIFI RECOVERY: softAP() failed; will retry.");
    logger.println("WIFI RECOVERY: softAP() failed; will retry.");
    WiFi.mode(WIFI_STA);
    return;
  }
  s_dns.start(53, "*", WiFi.softAPIP());
  s_hotspotActive = true;
  s_staPaused = false;
  s_lastClientMs = millis();
  s_lastRetryMs = millis();
  Serial.printf("WIFI RECOVERY: %s -> setup hotspot '%s' ON at http://%s/wifi-recover (saved credentials kept)\n",
                manual ? "status button" : "no WiFi for 2 min", apName.c_str(), WiFi.softAPIP().toString().c_str());
  logger.printf("WIFI RECOVERY: %s -> setup hotspot '%s' ON (saved credentials kept)\n",
                manual ? "status button" : "no WiFi for 2 min", apName.c_str());
}

static void stopHotspot() {
  s_dns.stop();
  WiFi.softAPdisconnect(true);               // AP off; station connection stays
  s_hotspotActive = false;
  s_manualUntilMs = 0;
  s_staPaused = false;
  s_justConnectedViaPortal = false;
  // Restart the reefdoserNN.local name on the (possibly new) network, like the
  // first-time provisioner does after it connects.
  String host = deviceID;
  host.toLowerCase();
  MDNS.end();
  if (MDNS.begin(host.c_str())) MDNS.addService("http", "tcp", 80);
  Serial.println("WIFI RECOVERY: setup hotspot OFF.");
  logger.println("WIFI RECOVERY: setup hotspot OFF.");
}

static void beginPending(const String& ssid, const String& pass) {
  s_pendSsid = ssid;
  s_pendPass = pass;
  s_pendingActive = true;
  s_pendingStartMs = millis();
  s_lastResult = "";
  s_staPaused = false;
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("WIFI RECOVERY: trying NEW credentials for '%s' (old ones are kept until these work)\n", ssid.c_str());
  logger.printf("WIFI RECOVERY: trying NEW credentials for '%s' (old ones kept until these work)\n", ssid.c_str());
}

// ----- web pages ------------------------------------------------------------
static const char PAGE_HEAD[] =
  "<!doctype html><html><head><meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>AIDoser WiFi</title>"
  "<style>body{font-family:sans-serif;margin:18px;max-width:420px}"
  "input{width:100%;padding:10px;margin:6px 0 14px;font-size:16px;box-sizing:border-box}"
  "button{padding:12px 18px;font-size:16px}.n{color:#555;font-size:14px}"
  ".e{color:#b00020}.o{color:#0a6b2d}</style>";

static void sendSetupPage() {
  if (!requestIsFromHotspot()) { server.send(403, "text/plain", "Setup hotspot not active."); return; }
  String h = PAGE_HEAD;
  h += "</head><body><h2>AIDoser WiFi setup</h2>";
  h += "<p class='n'>Device: " + htmlEsc(deviceID) + "<br>Dosing keeps running normally while you do this.</p>";
  if (s_lastResult.length()) h += "<p class='e'>" + htmlEsc(s_lastResult) + "</p>";
  h += "<form method='POST' action='/wifi-recover/save'>";
  h += "<label>WiFi network name</label><input name='ssid' maxlength='32' required value='" + htmlEsc(s_savedSsid) + "'>";
  h += "<label>WiFi password</label><input name='pass' type='password' maxlength='63' autocomplete='off'>";
  h += "<button type='submit'>Connect</button></form>";
  h += "<p class='n'>Your phone may disconnect from this hotspot for a few seconds while the AIDoser connects.</p>";
  h += "</body></html>";
  server.send(200, "text/html", h);
}

static void sendStatusPage() {
  if (!requestIsFromHotspot()) { server.send(403, "text/plain", "Setup hotspot not active."); return; }
  String h = PAGE_HEAD;
  if (s_pendingActive) h += "<meta http-equiv='refresh' content='4'>";
  h += "</head><body><h2>AIDoser WiFi setup</h2>";
  if (s_pendingActive) {
    h += "<p>Connecting to <b>" + htmlEsc(s_pendSsid) + "</b>... please wait.</p>";
  } else if (WiFi.status() == WL_CONNECTED && s_justConnectedViaPortal) {
    h += "<p class='o'><b>Connected to " + htmlEsc(WiFi.SSID()) + ".</b><br>IP address: " + WiFi.localIP().toString() + "</p>";
    h += "<p class='n'>This setup hotspot will turn off in a few seconds. Rejoin your normal WiFi and open the AIDoser dashboard.</p>";
  } else {
    if (s_lastResult.length()) h += "<p class='e'>" + htmlEsc(s_lastResult) + "</p>";
    h += "<p><a href='/wifi-recover'>Try again</a></p>";
  }
  h += "</body></html>";
  server.send(200, "text/html", h);
}

static void handleSave() {
  if (!requestIsFromHotspot()) { server.send(403, "text/plain", "Setup hotspot not active."); return; }
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  if (ssid.length() < 1 || ssid.length() > 32) {
    s_lastResult = "Network name must be 1-32 characters.";
    server.sendHeader("Location", "/wifi-recover", true);
    server.send(302, "text/plain", "");
    return;
  }
  if (pass.length() != 0 && (pass.length() < 8 || pass.length() > 63)) {
    s_lastResult = "WiFi password must be 8-63 characters (or empty for an open network).";
    server.sendHeader("Location", "/wifi-recover", true);
    server.send(302, "text/plain", "");
    return;
  }
  beginPending(ssid, pass);
  server.sendHeader("Location", "/wifi-recover/status", true);
  server.send(302, "text/plain", "");
}

static void handleCaptiveRedirect() {
  if (!requestIsFromHotspot()) { server.send(404, "text/plain", "Not found"); return; }
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/wifi-recover", true);
  server.send(302, "text/plain", "");
}

// ----- public API -----------------------------------------------------------
void wifiRecoveryBegin() {
  loadSaved();
  server.on("/wifi-recover", HTTP_GET, sendSetupPage);
  server.on("/wifi-recover/save", HTTP_POST, handleSave);
  server.on("/wifi-recover/status", HTTP_GET, sendStatusPage);
  // Phones probe these addresses to decide whether to pop up a sign-in page.
  static const char* const probes[] = {
    "/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
    "/connecttest.txt", "/ncsi.txt", "/redirect", "/canonical.html", "/success.txt", "/fwlink"
  };
  for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) server.on(probes[i], HTTP_GET, handleCaptiveRedirect);
  s_begun = true;
  Serial.printf("WIFI RECOVERY: ready (saved credentials %s)\n", s_hasSaved ? "present" : "absent -> first-time setup handles it");
  logger.printf("WIFI RECOVERY: ready (saved credentials %s)\n", s_hasSaved ? "present" : "absent");
}

bool wifiRecoveryHotspotActive() { return s_hotspotActive; }

void wifiRecoveryRequestHotspot(uint32_t minutes) {
  s_manualMinutes = minutes ? minutes : 10;
  s_manualRequested = true;
}

void wifiRecoveryTick() {
  if (!s_begun) return;
  const unsigned long now = millis();

  // No saved credentials = first-time setup, handled by the existing provisioner.
  // Keep the exact legacy behavior there: just the plain 30 s background reconnect.
  if (!s_hasSaved) {
    s_manualRequested = false;   // first-time setup already runs its own hotspot
    if (WiFi.status() != WL_CONNECTED && (s_lastRetryMs == 0 || now - s_lastRetryMs >= WIFI_RECOVERY_RETRY_MS)) {
      s_lastRetryMs = now;
      WiFi.reconnect();
    }
    return;
  }
  const bool connected = (WiFi.status() == WL_CONNECTED);

  // --- connection bookkeeping ---
  if (connected) {
    if (!s_wasConnected) { s_wasConnected = true; s_connectedSinceMs = now; }
    if (now - s_connectedSinceMs >= WIFI_RECOVERY_STABLE_MS) s_lost = false;   // really back
  } else {
    s_wasConnected = false;
    if (!s_lost) { s_lost = true; s_lostSinceMs = now; s_retryCount = 0; s_lastRetryMs = now; }
  }

  // --- result of a password typed into the setup page ---
  if (s_pendingActive) {
    if (connected && WiFi.SSID() == s_pendSsid) {
      saveCredsOnce(s_pendSsid, s_pendPass);                       // saved exactly once, after it worked
      s_pendingActive = false;
      s_justConnectedViaPortal = true;
      s_lastResult = "";
      Serial.printf("WIFI RECOVERY: new credentials for '%s' WORK -> saved.\n", s_pendSsid.c_str());
      logger.printf("WIFI RECOVERY: new credentials for '%s' work -> saved.\n", s_pendSsid.c_str());
    } else if (now - s_pendingStartMs > WIFI_RECOVERY_PENDING_MS) {
      s_pendingActive = false;
      s_lastResult = "Could not connect to '" + s_pendSsid + "'. Check the name and password and try again.";
      Serial.println("WIFI RECOVERY: new credentials did NOT connect; old saved credentials kept.");
      logger.println("WIFI RECOVERY: new credentials did NOT connect; old saved credentials kept.");
      WiFi.begin(s_savedSsid.c_str(), s_savedPass.c_str());        // go back to the saved ones
    }
  }

  // --- hotspot requested with the status button ---
  if (s_manualRequested) {
    s_manualRequested = false;
    if (!s_hotspotActive) startHotspot(true);
    if (s_hotspotActive) {
      s_manualUntilMs = now + (unsigned long)s_manualMinutes * 60000UL;
      if (s_manualUntilMs == 0) s_manualUntilMs = 1;
    }
  }

  // --- hotspot off -> maybe on ---
  if (!s_hotspotActive) {
    if (!connected && s_lost && now - s_lostSinceMs >= WIFI_RECOVERY_AFTER_MS) startHotspot();

    // Normal background reconnect (same 30 s cadence as before).
    if (!connected && !s_pendingActive && now - s_lastRetryMs >= WIFI_RECOVERY_RETRY_MS) {
      s_lastRetryMs = now;
      if (s_retryCount == 0 || (s_retryCount % 20) == 0) {
        Serial.println("WIFI RECOVERY: not connected, attempting background reconnect...");
        logger.printf("WIFI RECOVERY: not connected, attempting background reconnect (attempt %lu)...\n", s_retryCount + 1);
      }
      s_retryCount++;
      WiFi.reconnect();
    }
    return;
  }

  // --- hotspot is on ---
  s_dns.processNextRequest();

  // Done? Wait a moment after a portal-assisted connect so the phone can read the result.
  const unsigned long holdMs = s_justConnectedViaPortal ? 20000UL : 5000UL;
  if (connected && (s_manualUntilMs == 0 || s_justConnectedViaPortal) && now - s_connectedSinceMs >= holdMs) {
    stopHotspot();
    return;
  }
  // Opened by the status button: close after the chosen time, but never while a phone is on it.
  if (s_manualUntilMs != 0 && (long)(now - s_manualUntilMs) >= 0 && WiFi.softAPgetStationNum() == 0) {
    stopHotspot();
    return;
  }

  // While a phone is on the hotspot, pause the station retries (they make the
  // radio hop channels and kick the phone off). Credentials are NOT erased.
  const bool clients = WiFi.softAPgetStationNum() > 0;
  if (clients) s_lastClientMs = now;
  if (clients && !s_staPaused && !s_pendingActive && !connected) {
    s_staPaused = true;
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);                                 // keep credentials
  }
  if (s_staPaused && !clients && now - s_lastClientMs > 15000UL) {
    s_staPaused = false;
    WiFi.setAutoReconnect(true);
    WiFi.reconnect();
    s_lastRetryMs = now;
  }

  // Nobody connected to the hotspot: keep trying the saved credentials in the background.
  if (!connected && !s_pendingActive && !s_staPaused && !clients &&
      now - s_lastRetryMs >= WIFI_RECOVERY_RETRY_AP_MS) {
    s_lastRetryMs = now;
    s_retryCount++;
    WiFi.reconnect();
  }
}
