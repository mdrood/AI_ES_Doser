#include "Provisioner.h"

Provisioner::Provisioner() : _server(80), _configDone(false) {}

void Provisioner::startPortal(const char* apName) {
    WiFi.softAP(apName);
    
    // Captive Portal: Redirect all DNS requests to the ESP32 IP
    _dnsServer.start(53, "*", WiFi.softAPIP()); 

    // mDNS: Allow user to go to http://aidoser.local
    if (MDNS.begin("aidoser")) {
        Serial.println("mDNS responder started: http://aidoser.local");
    }

    _setupRoutes();
    _server.begin();
}

void Provisioner::handleClient() {
    _dnsServer.processNextRequest();
    _server.handleClient();
}

bool Provisioner::isConfigurationDone() {
    return _configDone;
}

void Provisioner::_setupRoutes() {
    _server.on("/", std::bind(&Provisioner::_handleRoot, this));
    _server.on("/save", HTTP_POST, std::bind(&Provisioner::_handleSave, this));
}

void Provisioner::_handleRoot() {
    String html = "<html><body><h1>ReefDoser Setup</h1>";
    html += "<form action='/save' method='POST'>";
    html += "<h3>WiFi Settings</h3>";
    html += "SSID: <input type='text' name='ssid'><br>";
    html += "Password: <input type='password' name='pass'><br>";
    html += "<h3>Pump Calibration (ml/min)</h3>";
    html += "Pump 0: <input type='number' step='0.1' name='p0' value='650'><br>";
    html += "Pump 1: <input type='number' step='0.1' name='p1' value='670'><br>";
    html += "Pump 2: <input type='number' step='0.1' name='p2' value='640'><br>";
    html += "Pump 3: <input type='number' step='0.1' name='p3' value='665'><br>";
    html += "<br><input type='submit' value='Save and Restart'>";
    html += "</form></body></html>";
    _server.send(200, "text/html", html);
}

void Provisioner::_handleSave() {
    Preferences prefs;
    prefs.begin("doser-settings", false);

    // Save WiFi
    prefs.putString("ssid", _server.arg("ssid"));
    prefs.putString("pass", _server.arg("pass"));

    // Save Pump Calibrations
    prefs.putFloat("p0", _server.arg("p0").toFloat());
    prefs.putFloat("p1", _server.arg("p1").toFloat());
    prefs.putFloat("p2", _server.arg("p2").toFloat());
    prefs.putFloat("p3", _server.arg("p3").toFloat());

    prefs.end();

    // The User-Facing HTML Response
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:sans-serif; text-align:center; padding:20px;} .box{border:1px solid #ccc; padding:15px; border-radius:10px; background:#f9f9f9;}</style></head>";
    html += "<body><h1>Settings Saved!</h1>";
    html += "<div class='box'>";
    html += "<h2>What to do now:</h2>";
    html += "<p>1. Connect your phone back to your <b>home WiFi</b>.</p>";
    html += "<p>2. Open your browser and go to:</p>";
    html += "<p style='font-size:1.2em; color:#007bff;'><b>http://aidoser.local</b></p>";
    html += "<p>3. The device will restart in 5 seconds.</p>";
    html += "</div></body></html>";
    
    _server.send(200, "text/html", html);
    
    Serial.println("Provisioning complete. Rebooting in 5s...");
    delay(5000); 
    _configDone = true; 
}