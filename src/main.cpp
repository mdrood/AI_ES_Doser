#include <Arduino.h>

// 1. SYSTEM LIBRARIES (MUST BE FIRST)
#include <FS.h>
#include <LittleFS.h>  
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// 2. YOUR CUSTOM HEADERS
#include "Doser.h"
#include "AI_Engine.h"
#include "StateMachine.h"
#include "Provisioner.h"
#include <EEPROM.h>

// 3. FIREBASE CONFIG
#include <FirebaseESP32.h>
#include <addons/RTDBHelper.h>
#include "calibration.h"
#include "ota.h"

#define FIREBASE_API_KEY "AIzaSyB4XtC5Pvxw6To58EKTLMADQLqR_hTZK0M"
#define FIREBASE_DB_URL "https://aiesdoser-default-rtdb.firebaseio.com"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

Provisioner provisioner;
String deviceID = "reefDoser1"; 

WebServer server(80);
Preferences prefs;

Doser doser;
CalibrationManager calManager(doser);
AIEngine ai;
StateManager state;
OtaManager ota;

float kalkBucket = 0.0, alkBucket  = 0.0, caBucket   = 0.0, mgBucket   = 0.0;
const float DOSING_THRESHOLD = 20.0; 

void connectToFirebase();
void tokenStatusCallback(TokenInfo info);
void streamTimeoutCallback(bool timeout);
// Note: StreamData is the correct type for your version's callback
void streamCallback(StreamData data); 

unsigned long lastSliceMillis = 0;

void tokenStatusCallback(TokenInfo info) {
    if (info.status == token_status_error) {
        Serial.printf("Token error: %s\n", info.error.message.c_str());
    } else {
        Serial.println("Token updated successfully");
    }
}

// FIXED: Only ONE streamCallback function
void streamCallback(StreamData data) {
    Serial.printf("Stream update: %s %s\n", data.dataPath().c_str(), data.dataType().c_str());

    if (data.dataType() == "json") {
        FirebaseJson &json = data.jsonObject();
        FirebaseJsonData result;
        
        if (json.get(result, "kalk")) ai.currentPlan.kalk = result.floatValue;
        if (json.get(result, "alk")) ai.currentPlan.alk = result.floatValue;
        if (json.get(result, "cacl2")) ai.currentPlan.cacl2 = result.floatValue;
        if (json.get(result, "mg")) ai.currentPlan.mg = result.floatValue;
        
        ai.currentPlan.active = true;
        Serial.println("Remote plan applied!");
    }

    // 2. Handle Calibration Commands from index.html
    if (data.dataPath() == "/commands/calibrate") {
        FirebaseJson &json = data.jsonObject();
        FirebaseJsonData pRes, dRes;
        
        json.get(pRes, "pump");     // index.html sends 1, 2, 3...
        json.get(dRes, "duration"); // index.html sends 60
        
        int pumpIdx = pRes.intValue - 1; 
        calManager.startCalibrationRun(pumpIdx, dRes.intValue * 1000);
    }
    //OTA
    if (data.dataPath() == "/commands/ota") {
    FirebaseJson &json = data.jsonObject();
    FirebaseJsonData res;
    
    json.get(res, "url");
    String firmwareUrl = res.stringValue;
    
    // Safety: only update if url is present
    if (firmwareUrl.length() > 10) {
        ota.updateFirmware(firmwareUrl);
    }
    }
}

void streamTimeoutCallback(bool timeout) {
    if (timeout) Serial.println("Stream timed out, resuming...");
}

void setup() {
    Serial.begin(115200);
    EEPROM.begin(512);
    
    // Open Preferences to grab the saved WiFi info
    prefs.begin("doser-settings", false);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();

    if (ssid == "") {
        Serial.println("No WiFi saved. Starting Hotspot...");
        provisioner.startPortal("ReefDoser_Setup");
        state.transitionTo(SystemState::PROVISIONING);
    } else {
        Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());

        // --- NEW: WAIT FOR CONNECTION ---
        int retryCount = 0;
        while (WiFi.status() != WL_CONNECTED && retryCount < 20) {
            delay(500);
            Serial.print(".");
            retryCount++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi Connected!");
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP()); // --- NEW: PRINT IP ---
            
            doser.begin();
            state.transitionTo(SystemState::IDLE);
            connectToFirebase();
        } else {
            Serial.println("\nWiFi Connection Failed. Reverting to Hotspot...");
            provisioner.startPortal("ReefDoser_Setup");
            state.transitionTo(SystemState::PROVISIONING);
        }
    }
    lastSliceMillis = millis();
}
void loop() {
    doser.tick();

    if (state.getCurrentState() == SystemState::PROVISIONING) {
        provisioner.handleClient();
        if (provisioner.isConfigurationDone()) {
            ESP.restart();
        }
        return;
    }

    unsigned long now = millis();

    // 10-Minute Scheduler
    if (now - lastSliceMillis >= 600000) { 
        lastSliceMillis = now;

        if (state.canDose() && ai.currentPlan.active) {
            kalkBucket += (ai.currentPlan.kalk  / 144.0);
            alkBucket  += (ai.currentPlan.alk   / 144.0);

            Serial.printf("BUCKETS: Kalk:%.2fml, Alk:%.2fml\n", kalkBucket, alkBucket);

            // Sync to Firebase
            String baseNode = "/devices/" + deviceID + "/buckets/";
            Firebase.setFloat(fbdo, (baseNode + "kalk").c_str(), kalkBucket);
            Firebase.setFloat(fbdo, (baseNode + "alk").c_str(), alkBucket);

            if (kalkBucket >= DOSING_THRESHOLD) {
                doser.doseMl(0, kalkBucket);
                kalkBucket = 0;
            }

            if (alkBucket >= DOSING_THRESHOLD && !doser.isPumpRunning(0)) { 
                doser.doseMl(1, alkBucket);
                alkBucket = 0;
            }
        }
    }

    // AI Recalculation (Every 1 hour)
    static unsigned long lastAIUpdate = 0;
    if (now - lastAIUpdate >= 3600000) {
        lastAIUpdate = now;
        ai.calculateNextPlan(1, 1.5, 0, 0, 8.2); 
        ai.currentPlan.active = true; 
    }
}

// FIXED: Removed .RTDB and using public Firebase methods
void connectToFirebase() {
    config.api_key = FIREBASE_API_KEY;
    config.database_url = FIREBASE_DB_URL;
    config.token_status_callback = tokenStatusCallback; 

    if (Firebase.signUp(&config, &auth, "", "")) {
        Serial.printf("Firebase Auth Success for %s\n", deviceID.c_str());
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);

        String path = "/devices/" + deviceID + "/plan";
        
        // Use Firebase.beginStream directly
        if (!Firebase.beginStream(fbdo, path.c_str())) {
            Serial.printf("Stream error: %s\n", fbdo.errorReason().c_str());
        }

        Firebase.setStreamCallback(fbdo, streamCallback, streamTimeoutCallback);
    } else {
        Serial.printf("Firebase Auth Failed: %s\n", config.signer.signupError.message.c_str());
    }
}