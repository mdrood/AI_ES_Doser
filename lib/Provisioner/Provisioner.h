#ifndef PROVISIONER_H
#define PROVISIONER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#include <ESPmDNS.h>
#include <DNSServer.h>

class Provisioner {
public:
    Provisioner();
    void startPortal(const char* apName);
    void handleClient();
    bool isConfigurationDone();
    bool shouldRestart();

private:
    WebServer _server;
    DNSServer _dnsServer;
    bool _configDone;
    bool _connectStarted;
    unsigned long _connectStartMs;
    unsigned long _restartAtMs;
    bool _connectedAnnounced;
    String _deviceName;
    String _mdnsName;

    void _setupRoutes();
    void _handleRoot();
    void _handleSave();
    void _handleConnectStatus();
    void _handleNotFound();
};

#endif
