#include "Provisioner.h"

Provisioner::Provisioner()
    : _server(80),
      _configDone(false),
      _connectStarted(false),
      _connectStartMs(0),
      _restartAtMs(0),
      _connectedAnnounced(false) {}

void Provisioner::startPortal(const char* apName) {
    // Keep setup AP alive while also allowing STA/home-WiFi connection.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apName);

    // Captive Portal: Redirect all DNS requests to the ESP32 AP IP.
    _dnsServer.start(53, "*", WiFi.softAPIP());

    // Convenience only. Do not rely on mDNS for first-time setup.
    if (MDNS.begin("aidoser")) {
        Serial.println("mDNS responder started: http://aidoser.local");
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
        if (MDNS.begin("aidoser")) {
            Serial.println("mDNS responder restarted on STA: http://aidoser.local");
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
    html += "SSID:<br><input type='text' name='ssid' required><br>";
    html += "Password:<br><input type='password' name='pass'><br>";
    html += "<h3>Pump Calibration (ml/min)</h3>";
    html += "Pump 0:<br><input type='number' step='0.1' name='p0' value='650'><br>";
    html += "Pump 1:<br><input type='number' step='0.1' name='p1' value='670'><br>";
    html += "Pump 2:<br><input type='number' step='0.1' name='p2' value='640'><br>";
    html += "Pump 3:<br><input type='number' step='0.1' name='p3' value='665'><br>";
    html += "<input type='submit' value='Save and Connect'>";
    html += "</form>";
    html += "</div></body></html>";

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
    html += "If your phone says <b>ReefDoser-Setup has no internet</b>, choose <b>Stay Connected</b> until the IP appears.";
    html += "</div>";

    html += "<p>Backup name after restart: <span class='url'>http://aidoser.local</span></p>";

    html += "<script>";
    html += "async function check(){";
    html += "try{";
    html += "let r=await fetch('/connect-status?ts='+Date.now(),{cache:'no-store'});";
    html += "let j=await r.json();";
    html += "let s=document.getElementById('status');";
    html += "if(j.connected){";
    html += "s.innerHTML='<h2 class=\"ok\">Connected!</h2>' +";
    html += "'<p><b>Write this IP down now.</b> AIDoser will restart in about 2 minutes.</p>' +";
    html += "'<p>After restart, reconnect your phone/computer to your home WiFi.</p>' +";
    html += "'<h3>Dashboard</h3><p class=\"url\">http://' + j.ip + '</p>' +";
    html += "'<h3>WebSerial</h3><p class=\"url\">http://' + j.ip + ':81/webserial</p>' +";
    html += "'<p><a href=\"http://' + j.ip + '\"><button>Try Dashboard</button></a></p>';";
    html += "}else if(j.failed){";
    html += "s.innerHTML='<h2 class=\"bad\">WiFi connection failed</h2><p>Reconnect to ReefDoser-Setup and check SSID/password.</p>';";
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
    json += "\"}";

    _server.send(200, "application/json", json);
}
