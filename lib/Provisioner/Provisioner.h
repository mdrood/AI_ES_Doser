#ifndef PROVISIONER_H
#define PROVISIONER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#include <ESPmDNS.h> // Add this include
#include <DNSServer.h> // Add this for the Captive Portal

class Provisioner {
public:
    Provisioner();
    void startPortal(const char* apName);
    void handleClient();
    bool isConfigurationDone();

private:
    WebServer _server;
    DNSServer _dnsServer;
    bool _configDone;
    void _setupRoutes();
    void _handleRoot();
    void _handleSave();
};

#endif