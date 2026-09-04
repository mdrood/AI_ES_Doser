#include "Provisioner.h"
#include <vector>
#include <algorithm>

Provisioner::Provisioner()
    : _server(80),
      _configDone(false),
      _connectStarted(false),
      _connectStartMs(0),
      _restartAtMs(0),
      _connectedAnnounced(false),
      _deviceName("AIDoser"),
      _mdnsName("reefDoser") {}

void Provisioner::startPortal(const char* apName) {
    // Keep the branded setup AP name, but use only reefDoserX for mDNS.
    _deviceName = (apName && apName[0] != '\0') ? String(apName) : String("AIDoser");

    _mdnsName = _deviceName;
    if (_mdnsName.startsWith("AIDoser-")) {
        _mdnsName.remove(0, 8);
    }
    if (_mdnsName.length() == 0) {
        _mdnsName = "reefDoser";
    }

    // Keep setup AP alive while also allowing STA/home-WiFi connection.
    WiFi.mode(WIFI_AP_STA);
    // Explicit rather than relying on the implicit default -- guarantees
    // 192.168.4.1 is genuinely correct rather than assumed correct.
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(_deviceName.c_str());

    // Captive Portal: Redirect all DNS requests to the ESP32 AP IP.
    _dnsServer.start(53, "*", WiFi.softAPIP());

    // Convenience only. Do not rely on mDNS for first-time setup.
    if (MDNS.begin(_mdnsName.c_str())) {
        Serial.printf("mDNS responder started: http://%s.local\n", _mdnsName.c_str());
    }

    _setupRoutes();
    _server.begin();
}

void Provisioner::handleClient() {
    _dnsServer.processNextRequest();
    _server.handleClient();

    // Once the ESP32 gets a home-network IP, keep the setup page alive
    // long enough for the customer to read/copy it, then allow main.cpp to reboot.
    if (_connectStarted && WiFi.status() == WL_CONNECTED && !_connectedAnnounced) {
        _connectedAnnounced = true;

        Serial.print("Provisioner WiFi connected. Home IP: ");
        Serial.println(WiFi.localIP());

        MDNS.end();
        if (MDNS.begin(_mdnsName.c_str())) {
            Serial.printf("mDNS responder restarted on STA: http://%s.local\n", _mdnsName.c_str());
        }

        // Keep the final setup page alive for 2 minutes after IP is known.
        _restartAtMs = millis() + 120000UL;
    }
}

bool Provisioner::isConfigurationDone() {
    return _configDone;
}

bool Provisioner::shouldRestart() {
    return (_restartAtMs > 0 && (long)(millis() - _restartAtMs) >= 0);
}

void Provisioner::_setupRoutes() {
    _server.on("/", std::bind(&Provisioner::_handleRoot, this));
    _server.on("/save", HTTP_POST, std::bind(&Provisioner::_handleSave, this));
    _server.on("/connect-status", HTTP_GET, std::bind(&Provisioner::_handleConnectStatus, this));

    // Added 2026-08-06: lets the setup page show a list of nearby networks
    // to pick from instead of requiring the SSID to be typed in by hand.
    _server.on("/scan", HTTP_GET, std::bind(&Provisioner::_handleScan, this));

    // Fixed 2026-07-25: without this, iOS/Android's captive-portal probe
    // requests (e.g. captive.apple.com/hotspot-detect.html,
    // connectivitycheck.gstatic.com/generate_204) hit a plain 404 instead
    // of a redirect, since none of the three routes above match those
    // paths. The OS then concludes this network has nothing useful and
    // often won't auto-open the sign-in browser at all -- exactly the
    // "connected to the hotspot, but nothing happens" symptom. A 302
    // redirect to "/" for any unmatched path is what a captive portal is
    // actually expected to return.
    _server.onNotFound(std::bind(&Provisioner::_handleNotFound, this));
}

void Provisioner::_handleNotFound() {
    // Standard captive-portal response: redirect any unmatched path to the
    // setup page itself, using the AP's own IP rather than a relative path
    // (some OS probes check the response is same-origin / a real address,
    // not just a 3xx status).
    _server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    _server.send(302, "text/plain", "");
}

// Added 2026-08-06: lists nearby WiFi networks so the setup page can offer
// a pick-from-a-list dropdown instead of requiring the SSID to be typed by
// hand. Uses the synchronous WiFi.scanNetworks() -- this blocks for a few
// seconds, but that's an acceptable, normal tradeoff during one-time device
// setup (before any water dosing is happening at all), not something
// running during live operation. Deduplicates repeated SSIDs (common with
// mesh routers broadcasting the same name from multiple access points,
// which would otherwise show the same network several times) by keeping
// only the strongest-signal instance of each, and sorts strongest-first so
// the customer's own router is likely to be at or near the top.
void Provisioner::_handleScan() {
    int n = WiFi.scanNetworks();

    struct NetworkEntry {
        String ssid;
        int32_t rssi;
        bool open;
    };
    std::vector<NetworkEntry> networks;

    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue; // hidden networks aren't pickable from a list anyway

        bool found = false;
        for (auto& existing : networks) {
            if (existing.ssid == ssid) {
                found = true;
                if (WiFi.RSSI(i) > existing.rssi) {
                    existing.rssi = WiFi.RSSI(i);
                    existing.open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
                }
                break;
            }
        }
        if (!found) {
            networks.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
        }
    }

    std::sort(networks.begin(), networks.end(), [](const NetworkEntry& a, const NetworkEntry& b) {
        return a.rssi > b.rssi;
    });

    String json = "[";
    for (size_t i = 0; i < networks.size(); i++) {
        if (i > 0) json += ",";
        String escapedSsid = networks[i].ssid;
        escapedSsid.replace("\\", "\\\\");
        escapedSsid.replace("\"", "\\\"");
        json += "{\"ssid\":\"" + escapedSsid + "\",\"rssi\":" + String(networks[i].rssi) + ",\"open\":" + (networks[i].open ? "true" : "false") + "}";
    }
    json += "]";

    WiFi.scanDelete();
    _server.send(200, "application/json", json);
}

void Provisioner::_handleRoot() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:sans-serif;padding:20px;background:#f4f4f4;}";
    html += ".box{background:white;padding:20px;border-radius:12px;max-width:520px;margin:auto;}";
    html += "input{width:100%;padding:10px;margin:6px 0 14px 0;box-sizing:border-box;}";
    html += "input[type=submit]{background:#007bff;color:white;border:0;border-radius:8px;font-weight:bold;}";
    html += "</style></head><body>";
    html += "<div class='box'>";
    html += "<h1>ReefDoser Setup</h1>";
    html += "<form action='/save' method='POST'>";
    html += "<h3>WiFi Settings</h3>";
    html += "<div id='scanStatus' style='color:#666;font-size:0.9em;margin-bottom:4px;'>Scanning for nearby networks...</div>";
    html += "<select id='ssidPicker' onchange=\"if(this.value)document.getElementsByName('ssid')[0].value=this.value;\">";
    html += "<option value=''>-- Select a network (or type below) --</option>";
    html += "</select>";
    html += "SSID:<br><input type='text' name='ssid' required><br>";
    html += "Password:<br><input type='password' name='pass'><br>";
    html += "<h3>Pump Calibration (ml/min)</h3>";
    html += "Pump 0:<br><input type='number' step='0.1' name='p0' value='650'><br>";
    html += "Pump 1:<br><input type='number' step='0.1' name='p1' value='670'><br>";
    html += "Pump 2:<br><input type='number' step='0.1' name='p2' value='640'><br>";
    html += "Pump 3:<br><input type='number' step='0.1' name='p3' value='665'><br>";
    html += "<input type='submit' value='Save and Connect'>";
    html += "</form>";
    html += "</div>";

    html += "<script>";
    html += "(async function(){";
    html += "  const statusEl = document.getElementById('scanStatus');";
    html += "  const picker = document.getElementById('ssidPicker');";
    html += "  try {";
    html += "    const r = await fetch('/scan');";
    html += "    const networks = await r.json();";
    html += "    if (networks.length === 0) {";
    html += "      statusEl.textContent = 'No networks found nearby -- type your SSID below.';";
    html += "    } else {";
    html += "      statusEl.textContent = networks.length + ' network' + (networks.length === 1 ? '' : 's') + ' found. Not seeing yours? Type it in below instead.';";
    html += "      for (const net of networks) {";
    html += "        const opt = document.createElement('option');";
    html += "        opt.value = net.ssid;";
    html += "        const bars = net.rssi > -60 ? '\\u2588\\u2588\\u2588' : (net.rssi > -75 ? '\\u2588\\u2588\\u2591' : '\\u2588\\u2591\\u2591');";
    html += "        opt.textContent = net.ssid + '  ' + bars + (net.open ? '' : '  \\uD83D\\uDD12');";
    html += "        picker.appendChild(opt);";
    html += "      }";
    html += "    }";
    html += "  } catch (e) {";
    html += "    statusEl.textContent = 'Could not scan for networks -- type your SSID below.';";
    html += "  }";
    html += "})();";
    html += "</script>";

    html += "</body></html>";

    _server.send(200, "text/html", html);
}

void Provisioner::_handleSave() {
    Preferences prefs;
    prefs.begin("doser-settings", false);

    String ssid = _server.arg("ssid");
    String pass = _server.arg("pass");

    // Save WiFi.
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);

    // Save pump calibrations.
    prefs.putFloat("p0", _server.arg("p0").toFloat());
    prefs.putFloat("p1", _server.arg("p1").toFloat());
    prefs.putFloat("p2", _server.arg("p2").toFloat());
    prefs.putFloat("p3", _server.arg("p3").toFloat());

    prefs.end();

    // Start connecting in background while the setup AP remains alive.
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    _connectStarted = true;
    _connectStartMs = millis();
    _restartAtMs = 0;
    _connectedAnnounced = false;
    _configDone = true;

    Serial.print("Provisioner saved WiFi. Connecting to SSID: ");
    Serial.println(ssid);

    // Send the final page immediately. It will poll /connect-status until
    // WiFi.localIP() is real, then display the actual dashboard/WebSerial URLs.
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:sans-serif;text-align:center;padding:20px;background:#f4f4f4;}";
    html += ".box{background:white;padding:20px;border-radius:12px;max-width:560px;margin:auto;}";
    html += ".url{font-size:1.15em;color:#007bff;word-wrap:break-word;font-weight:bold;}";
    html += ".bad{color:#b00020;font-weight:bold;}";
    html += ".ok{color:#087b20;font-weight:bold;}";
    html += "button{padding:12px 16px;border:0;border-radius:8px;background:#007bff;color:white;font-weight:bold;}";
    html += ".note{background:#fff3cd;border:1px solid #ffe69c;border-radius:10px;padding:12px;margin-top:12px;}";
    html += "</style></head><body>";

    html += "<div class='box'>";
    html += "<h1>AIDoser Setup Saved</h1>";
    html += "<p>Stay on this page. AIDoser is connecting to:</p>";
    html += "<p><b>" + ssid + "</b></p>";

    html += "<div id='status'>";
    html += "<h2>Connecting...</h2>";
    html += "<p>Waiting for home WiFi IP address.</p>";
    html += "</div>";

    html += "<div class='note'>";
    html += "If your phone says <b>" + _deviceName + " has no internet</b>, choose <b>Stay Connected</b> until the IP appears.";
    html += "</div>";

    html += "<p>Backup name after restart: <span class='url'>http://" + _mdnsName + ".local</span></p>";

    html += "<script>";
    html += "async function check(){";
    html += "try{";
    html += "let r=await fetch('/connect-status?ts='+Date.now(),{cache:'no-store'});";
    html += "let j=await r.json();";
    html += "let s=document.getElementById('status');";
    html += "if(j.connected){";
    html += "s.innerHTML='<h2 class=\"ok\">Connected!</h2>' +";
    html += "'<p><b>Write this down now.</b> AIDoser will restart in about 2 minutes.</p>' +";
    html += "'<p>After restart, reconnect your phone/computer to your home WiFi.</p>' +";
    html += "'<h3>Dashboard</h3><p class=\"url\">http://' + j.ip + '</p>' +";
    html += "'<p class=\"url\">or http://' + j.mdnsHost + '.local</p>' +";
    html += "'<h3>WebSerial</h3><p class=\"url\">http://' + j.ip + ':81/webserial</p>' +";
    html += "'<p><a href=\"http://' + j.ip + '\"><button>Try Dashboard</button></a></p>';";
    html += "}else if(j.failed){";
    html += "s.innerHTML='<h2 class=\"bad\">WiFi connection failed</h2><p>Reconnect to " + _deviceName + " and check SSID/password.</p>';";
    html += "}else{";
    html += "s.innerHTML='<h2>Connecting...</h2><p>Elapsed: '+j.elapsedSec+' seconds</p><p>IP: waiting...</p>';";
    html += "setTimeout(check,2000);";
    html += "}";
    html += "}catch(e){setTimeout(check,2000);}";
    html += "}";
    html += "setTimeout(check,1000);";
    html += "</script>";

    html += "</div></body></html>";

    _server.send(200, "text/html", html);
}

void Provisioner::_handleConnectStatus() {
    bool connected = WiFi.status() == WL_CONNECTED;
    unsigned long elapsedSec = _connectStarted ? ((millis() - _connectStartMs) / 1000UL) : 0;
    bool failed = _connectStarted && !connected && elapsedSec >= 45;

    String ip = connected ? WiFi.localIP().toString() : "";

    String json = "{";
    json += "\"connected\":";
    json += connected ? "true" : "false";
    json += ",\"failed\":";
    json += failed ? "true" : "false";
    json += ",\"elapsedSec\":";
    json += String(elapsedSec);
    json += ",\"ip\":\"";
    json += ip;
    json += "\",\"dashboardUrl\":\"";
    json += connected ? ("http://" + ip) : "";
    json += "\",\"webSerialUrl\":\"";
    json += connected ? ("http://" + ip + ":81/webserial") : "";
    json += "\",\"mdnsHost\":\"";
    json += _mdnsName;
    json += "\",\"mdnsDashboardUrl\":\"";
    json += connected ? ("http://" + _mdnsName + ".local") : "";
    json += "\"}";

    _server.send(200, "application/json", json);
}
