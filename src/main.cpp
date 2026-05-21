#include <Arduino.h>

// 1. SYSTEM LIBRARIES (MUST BE FIRST)
#include <FS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "global_config.h"
#include <esp_task_wdt.h>
#include "esp_system.h"

// 2. YOUR CUSTOM HEADERS
#include "Doser.h"
#include "AI_Engine.h"
#include "StateMachine.h"
#include "Provisioner.h"
#include "logger.h"
#include <EEPROM.h>

// 3. FIREBASE CONFIG
#include <FirebaseESP32.h>
#include <addons/RTDBHelper.h>
#include "calibration.h"
#include "ota.h"
#include <time.h> // Native ESP32 time library

#include "apexapi.h"
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>

#define FIREBASE_API_KEY "AIzaSyB4XtC5Pvxw6To58EKTLMADQLqR_hTZK0M"
#define FIREBASE_DB_URL "https://aiesdoser-default-rtdb.firebaseio.com"
//TODO for firware update
#define FW_VERSION "10.2.0"

#include "Dashboard.h"

// --- DEFINITIONS (Allocating actual memory for the Truth) ---
bool emergencyStop = false;

// Local operating mode for the standalone page: 0=OFF, 1=AUTO, 2=MAN
int systemMode = 1;

// --- Version 2: Smoothing & Lighting Config ---
struct LightConfig {
    int source = 0;    // 0 = Timer, 1 = Apex
    int start = 8;     // 8 AM
    int end = 20;      // 8 PM
    String outlet = "Light_7_1";
} lightConfig;

// Historical Smoothing Buffer
float alkHistory[5] = {0, 0, 0, 0, 0};
int alkIdx = 0;
float targetAlk = 8.5f; // Set your desired target here
float targetCa  = 450.0f;
float targetMg  = 1440.0f;

// AI starts from this known daily demand, then adjusts up/down from water tests.
float baselineKalkMlDay  = 0.0f;
float baselineCacl2MlDay = 0.0f;
float baselineNaohMlDay  = 0.0f;
float baselineMgMlDay    = 0.0f;
String baselineCoralLoad = "custom";

// 1..6 dosing implementation used for pump mapping
int dosingMode = 1;

bool apexEnabled = false;

float currentTempF = 0.0f;
float currentPh = 0.0f;
float currentCond = 0.0f;
float currentPpt = 0.0f;
float currentSg = 0.0f;
float currentAlk = 0.0f;
float currentCa = 0.0f;
float currentMg = 0.0f;

float DOSING_THRESHOLD = 1.0f; // Mutable now, default to 1ml
float pumpBuckets[4] = {0.0f, 0.0f, 0.0f, 0.0f};
float maxDoseLimit = 15.0f;    // Safety rail

String lastApexDate = "";
String apexIp = "";

float pumpFlowRates[4] = {675.0f, 645.0f, 50.0f, 50.0f};
ManualTest lastLocalTest;
bool hasSavedManualTest = false;

ApexApi apex;

FirebaseData streamFbdo;
FirebaseData writeFbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseStarted = false;
bool tankVolumePublishedThisBoot = false;
unsigned long lastTankVolumePublishAttemptMs = 0;

// Firmware version publish guard.
// The first Firebase.setString can fail silently if called before Firebase.ready(),
// so loop() retries until the dashboard-visible fwVersion write succeeds.
bool fwVersionPublished = false;
unsigned long lastFwVersionPublishAttemptMs = 0;

// Push Apex/state mirrors to Firebase on a timer so RTDB is not hammered.
// Local dashboard still updates immediately from currentTemp/currentPh/currentAlk/etc.
const unsigned long FIREBASE_MIRROR_INTERVAL_MS = 300000UL; // 5 minutes
unsigned long lastFirebaseMirrorMs = 0;

// Low-cost AI plan publishing: write dosingMlPerDay only when the plan changes
// by more than 10%, or once per day as a refresh.
float lastPublishedPlanMlDay[6] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
String lastPlanPublishDate = "";
const float PLAN_PUBLISH_CHANGE_FRACTION = 0.10f;

// Bucket accumulation is tied to the dosing slice interval, not the 1-hour AI timer.
// This lets fresh Apex/manual AI plans fill buckets promptly while avoiding double-fills.
const unsigned long AI_BUCKET_INTERVAL_MS = 600000UL; // 10 minutes = 144 slots/day
unsigned long lastAiBucketAddMs = 0;


Provisioner provisioner;
//TODO new customer
String deviceID = "reefDoser2";

WebServer server(80);
AsyncWebServer serialServer(81);
Preferences prefs;

Doser doser;
// Global logger instance is defined in lib/Logger/logger.cpp via extern Logger logger;
CalibrationManager calManager(doser);
AIEngine ai;
StateManager state;
OtaManager ota;

float TANK_VOLUME_L = 1135.6f; // Default to 300 Gallons



float kalkBucket = 0.0f, alkBucket = 0.0f, caBucket = 0.0f, mgBucket = 0.0f;


unsigned long lastSliceMillis = 0;

const char* GOOGLE_LOG_URL ="https://script.google.com/macros/s/AKfycbzXjvrBBfkzdYW6BsBreZXV8lznugdBzwyS1jrxPXgs5zoFfi20-RVGqlsarmzbuUmsAw/exec";

// Calibration timed runs are intentionally time-based, not mL-based.
// This prevents a bad/old calibration value from making a 60-second calibration run
// stop after only a few seconds.
bool calibrationRunActive[4] = {false, false, false, false};
unsigned long calibrationRunUntilMs[4] = {0, 0, 0, 0};

struct DailyStats {
    float alkSum = 0;
    float caSum = 0;
    float mgSum = 0;
    float phSum = 0;
    float tempSum = 0;
    float pptSum = 0;
    float sgSum = 0;
    float totalDose = 0;
    int count = 0;
};

void connectToFirebase();
void tokenStatusCallback(TokenInfo info);
void streamTimeoutCallback(bool timeout);
void streamCallback(StreamData data);
void syncAllTruths();

void handleRoot();
void handleGetStatus();
void handleGetMode();
void handlePostMode();
void handleGetDosingMode();
void handlePostDosingMode();
void handlePostManualTest();
void handlePostApexLocal();
void handlePostCalibration();
void handlePostCalibrationRun();
void handlePostLiveDose();
void handlePostEmergencyStop();
void handlePostResetWifi();
void handlePostAiBaseline();
void applyAiBaselineToEngine();

void saveManualTestLocally(float alk, float ca, float mg, float ph);
void loadManualTestLocally();
void loadLocalSettings();
void loadFlowRates();
void saveFlowRate(int idx, float flowMlPerMin);
void mirrorStatusToFirebase();
bool publishTankVolumeToFirebase(const char* source);
int getLocalHour();
void updateDailyAverages();
void pushDailyReport();
void loadDosingState();
void saveDosingState();
void addCurrentAiPlanToBuckets(const char* source, bool force = false);
void publishAiPlanIfNeeded(const char* source, bool force = false);
void publishFirmwareVersionIfReady(bool force = false);

const char* flowPrefKeyForIndex(int idx) {
    switch (idx) {
        case 0: return "flow_p1";
        case 1: return "flow_p2";
        case 2: return "flow_p3";
        case 3: return "flow_p4";
        default: return "flow_p1";
    }
}

const char* pumpKeyForPhysicalIndex(int idx) {
    switch (dosingMode) {
        case 1:
            return (idx == 0) ? "kalk" : "unused";
        case 2:
            return (idx == 0) ? "afr" : "unused";
        case 3:
            switch (idx) {
                case 0: return "kalk";
                case 1: return "afr";
                case 2: return "mg";
                default: return "unused";
            }
        case 4:
            switch (idx) {
                case 0: return "alk";
                case 1: return "ca";
                case 2: return "mg";
                default: return "unused";
            }
        case 5:
            switch (idx) {
                case 0: return "kalk";
                case 1: return "alk";
                case 2: return "ca";
                case 3: return "mg";
                default: return "unused";
            }
        case 6:
            switch (idx) {
                case 0: return "kalk";
                case 1: return "cacl2";
                case 2: return "naoh";
                case 3: return "mg";
                default: return "unused";
            }
        default:
            return (idx == 0) ? "kalk" : "unused";
    }
}

int pumpCountForCurrentDosingMode() {
    switch (dosingMode) {
        case 1:
        case 2:
            return 1;
        case 3:
        case 4:
            return 3;
        case 5:
        case 6:
            return 4;
        default:
            return 1;
    }
}

struct dailyStats {
    float tempSum = 0.0f;
    float phSum = 0.0f;
    float alkSum = 0.0f;
    float caSum = 0.0f;
    float mgSum = 0.0f;
    float pptSum = 0.0f;
    float sgSum = 0.0f;
    int count = 0;
} dailyStats;

bool reportPushedToday = false;
extern float dailyDoseTotals[4]; // Links to Doser.cpp

bool isValidSystemMode(int mode) {
    return mode >= 0 && mode <= 2;
}

bool isValidDosingMode(int mode) {
    return mode >= 1 && mode <= 6;
}

const char* resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:   return "UNKNOWN";
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXTERNAL";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "OTHER";
  }
}

void applyAiBaselineToEngine() {
    ai.setBaselineDemand(baselineKalkMlDay, baselineCacl2MlDay, baselineNaohMlDay, baselineMgMlDay);
}

void tokenStatusCallback(TokenInfo info) {
    if (info.status == token_status_error) {
        Serial.printf("Token error: %s\n", info.error.message.c_str());
        logger.printf("Token error: %s\n", info.error.message.c_str());
        //logger.printf("Token error: %s\n", info.error.message.c_str());
    } else {
        Serial.println("Token updated successfully");
        logger.println("Token updated successfully");
    }
}

void saveDosingState() {
    prefs.begin("doser-state", false);
    // Save Buckets
    prefs.putBytes("buckets", &pumpBuckets, sizeof(pumpBuckets));
    // Save Daily Stats
    prefs.putBytes("stats", &dailyStats, sizeof(dailyStats));
    // Save Daily Totals (for the graph)
    prefs.putBytes("totals", &dailyDoseTotals, sizeof(dailyDoseTotals));
    prefs.end();
}

void loadDosingState() {
    prefs.begin("doser-state", true); // Read-only mode
    if (prefs.isKey("buckets")) {
        prefs.getBytes("buckets", &pumpBuckets, sizeof(pumpBuckets));
        prefs.getBytes("stats", &dailyStats, sizeof(dailyStats));
        prefs.getBytes("totals", &dailyDoseTotals, sizeof(dailyDoseTotals));
    }
    prefs.end();
}


bool planPublishChangeIsSignificant(float oldVal, float newVal) {
    if (oldVal < 0.0f) return true;
    if (fabsf(newVal) < 0.01f && fabsf(oldVal) < 0.01f) return false;
    float denom = fabsf(oldVal) > 1.0f ? fabsf(oldVal) : 1.0f;
    return (fabsf(newVal - oldVal) / denom) >= PLAN_PUBLISH_CHANGE_FRACTION;
}

void publishAiPlanIfNeeded(const char* source, bool force) {
    if (WiFi.status() != WL_CONNECTED || !firebaseStarted || !Firebase.ready()) return;

    float planVals[6] = {
        ai.currentPlan.kalk,
        ai.currentPlan.afr,
        ai.currentPlan.alk,
        ai.currentPlan.cacl2,
        ai.currentPlan.naoh,
        ai.currentPlan.mg
    };

    // Do not let a boot-time/empty AI plan overwrite a good cloud plan with zeros.
    bool anyNonZero = false;
    for (int i = 0; i < 6; ++i) {
        if (fabsf(planVals[i]) >= 0.01f) {
            anyNonZero = true;
            break;
        }
    }
    if (!anyNonZero && !force) return;

    bool changed = force;
    for (int i = 0; i < 6; ++i) {
        if (planPublishChangeIsSignificant(lastPublishedPlanMlDay[i], planVals[i])) {
            changed = true;
            break;
        }
    }

    char today[11] = "";
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        strftime(today, sizeof(today), "%Y-%m-%d", &timeinfo);
        if (lastPlanPublishDate != String(today)) changed = true;
    }

    if (!changed) return;

    FirebaseJson json;
    json.set("kalk", planVals[0]);
    json.set("afr", planVals[1]);
    json.set("alk", planVals[2]);
    json.set("cacl2", planVals[3]);
    json.set("naoh", planVals[4]);
    json.set("mg", planVals[5]);
    json.set("updatedAt", (uint32_t)(millis() / 1000UL));
    json.set("source", source ? source : "AI");

    String path = "/devices/" + deviceID + "/state/dosingMlPerDay";
    if (Firebase.updateNode(writeFbdo, path.c_str(), json)) {
        for (int i = 0; i < 6; ++i) lastPublishedPlanMlDay[i] = planVals[i];
        if (strlen(today) > 0) lastPlanPublishDate = String(today);
        Serial.printf("Firebase plan OK [%s]: kalk=%.2f afr=%.2f alk=%.2f cacl2=%.2f naoh=%.2f mg=%.2f\n",
                      source ? source : "AI", planVals[0], planVals[1], planVals[2], planVals[3], planVals[4], planVals[5]);
        logger.printf("Firebase plan OK [%s]: kalk=%.2f afr=%.2f alk=%.2f cacl2=%.2f naoh=%.2f mg=%.2f\n",
                         source ? source : "AI", planVals[0], planVals[1], planVals[2], planVals[3], planVals[4], planVals[5]);
    } else {
        Serial.printf("Firebase plan skipped/failed [%s]: %s\n", source ? source : "AI", writeFbdo.errorReason().c_str());
        logger.printf("Firebase plan skipped/failed [%s]: %s\n", source ? source : "AI", writeFbdo.errorReason().c_str());
    }
}

void addCurrentAiPlanToBuckets(const char* source, bool force) {
    unsigned long nowMs = millis();
    if (!force && lastAiBucketAddMs != 0 && (nowMs - lastAiBucketAddMs) < AI_BUCKET_INTERVAL_MS) {
        return;
    }
    lastAiBucketAddMs = nowMs;

    Serial.printf("PLAN [%s]: kalk/day=%.2f afr/day=%.2f alk/day=%.2f cacl2/day=%.2f naoh/day=%.2f mg/day=%.2f\n",
                  source ? source : "AI",
                  ai.currentPlan.kalk,
                  ai.currentPlan.afr,
                  ai.currentPlan.alk,
                  ai.currentPlan.cacl2,
                  ai.currentPlan.naoh,
                  ai.currentPlan.mg);
    logger.printf("PLAN [%s]: kalk/day=%.2f afr/day=%.2f alk/day=%.2f cacl2/day=%.2f naoh/day=%.2f mg/day=%.2f\n",
                     source ? source : "AI",
                     ai.currentPlan.kalk,
                     ai.currentPlan.afr,
                     ai.currentPlan.alk,
                     ai.currentPlan.cacl2,
                     ai.currentPlan.naoh,
                     ai.currentPlan.mg);

    switch (dosingMode) {
        case 1:
            pumpBuckets[0] += ai.currentPlan.kalk / 144.0f;
            break;

        case 2:
            pumpBuckets[0] += ai.currentPlan.afr / 144.0f;
            break;

        case 3:
            pumpBuckets[0] += ai.currentPlan.kalk / 144.0f;
            pumpBuckets[1] += ai.currentPlan.afr  / 144.0f;
            pumpBuckets[2] += ai.currentPlan.mg   / 144.0f;
            break;

        case 4:
            pumpBuckets[0] += ai.currentPlan.alk   / 144.0f;
            pumpBuckets[1] += ai.currentPlan.cacl2 / 144.0f;
            pumpBuckets[2] += ai.currentPlan.mg    / 144.0f;
            break;

        case 5:
            pumpBuckets[0] += ai.currentPlan.kalk  / 144.0f;
            pumpBuckets[1] += ai.currentPlan.alk   / 144.0f;
            pumpBuckets[2] += ai.currentPlan.cacl2 / 144.0f;
            pumpBuckets[3] += ai.currentPlan.mg    / 144.0f;
            break;

        case 6:
            pumpBuckets[0] += ai.currentPlan.kalk  / 144.0f;
            pumpBuckets[1] += ai.currentPlan.cacl2 / 144.0f;
            pumpBuckets[2] += ai.currentPlan.naoh  / 144.0f;
            pumpBuckets[3] += ai.currentPlan.mg    / 144.0f;
            break;
    }

    saveDosingState();

    Serial.printf("BUCKETS [%s]: P1=%.2f P2=%.2f P3=%.2f P4=%.2f\n",
                  source ? source : "AI", pumpBuckets[0], pumpBuckets[1], pumpBuckets[2], pumpBuckets[3]);
    logger.printf("BUCKETS [%s]: P1=%.2f P2=%.2f P3=%.2f P4=%.2f\n",
                     source ? source : "AI", pumpBuckets[0], pumpBuckets[1], pumpBuckets[2], pumpBuckets[3]);
}

// 2. Logic to determine Light State
bool isLightsOn() {
    if (lightConfig.source == 1) { 
        return apex.getLightStatus();
    }
    int hr = getLocalHour();
    return (hr >= lightConfig.start && hr < lightConfig.end);
}


int getLocalHour() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        return 0; // Fallback if sync hasn't happened yet
    }
    return timeinfo.tm_hour; // Returns 0-23
}

void serviceWatchdogAndUi(uint16_t pauseMs = 5) {
    esp_task_wdt_reset();
    server.handleClient();
    yield();
    if (pauseMs > 0) {
        delay(pauseMs);
    }
    yield();
    esp_task_wdt_reset();
}

bool handleOtaFirmwareUrl(const String& firmwareUrl, const char* source) {
    String requiredFolder = "/devices/" + deviceID + "/";

    if (firmwareUrl.length() <= 10) {
        Serial.printf("OTA ignored from %s: missing firmware URL\n", source ? source : "stream");
        logger.printf("OTA ignored from %s: missing firmware URL\n", source ? source : "stream");
        return false;
    }

    if (firmwareUrl.indexOf(requiredFolder) < 0) {
        Serial.println("OTA BLOCKED: firmware URL does not match this device.");
        Serial.print("This device: ");
        Serial.println(deviceID);
        Serial.print("Required URL folder: ");
        Serial.println(requiredFolder);
        Serial.print("Received URL: ");
        Serial.println(firmwareUrl);

        logger.println("OTA BLOCKED: firmware URL does not match this device.");
        logger.print("This device: ");
        logger.println(deviceID);
        logger.print("Required URL folder: ");
        logger.println(requiredFolder);
        logger.print("Received URL: ");
        logger.println(firmwareUrl);
        return false;
    }

    Serial.print("OTA Triggered for ");
    Serial.println(deviceID);
    Serial.print("Starting OTA Update from: ");
    Serial.println(firmwareUrl);

    logger.print("OTA Triggered for ");
    logger.println(deviceID);
    logger.print("Starting OTA Update from: ");
    logger.println(firmwareUrl);

    // Make OTA one-shot: clear the Firebase command before starting OTA so
    // stream reconnects do not replay the same /commands/ota node forever.
    String otaCommandPath = "/devices/" + deviceID + "/commands/ota";
    if (Firebase.deleteNode(writeFbdo, otaCommandPath.c_str())) {
        Serial.println("OTA command cleared from Firebase.");
        logger.println("OTA command cleared from Firebase.");
    } else {
        Serial.print("OTA command clear failed: ");
        Serial.println(writeFbdo.errorReason());
        logger.print("OTA command clear failed: ");
        logger.println(writeFbdo.errorReason());
    }

    delay(250);
    yield();
    ota.updateFirmware(firmwareUrl);
    return true;
}

void streamCallback(StreamData data) {
    Serial.printf("Stream update: %s\n", data.dataPath().c_str());
    logger.printf("Stream update: %s\n", data.dataPath().c_str());

    // We stream /devices/<deviceID>/commands. Firebase may send an initial root
    // snapshot as "/". If /commands/ota already existed before the stream
    // attached, the OTA command arrives inside this root JSON snapshot, not as
    // a separate /ota event. Do not discard root until we check for ota/url.
    if (data.dataPath() == "/") {
        FirebaseJson &json = data.jsonObject();
        FirebaseJsonData res;

        if (json.get(res, "ota/url") && res.stringValue.length() > 0) {
            Serial.println("OTA found inside root stream snapshot.");
            logger.println("OTA found inside root stream snapshot.");
            handleOtaFirmwareUrl(res.stringValue, "root snapshot");
        } else {
            Serial.println("Root stream snapshot received; no ota/url found.");
            logger.println("Root stream snapshot received; no ota/url found.");
        }
        return;
    }

    if (data.dataPath() == "/ota") {
        FirebaseJson &json = data.jsonObject();
        FirebaseJsonData res;
        json.get(res, "url");
        handleOtaFirmwareUrl(res.stringValue, "/ota stream");
        return;
    }

    if (data.dataPath().indexOf("/logs/manualTests") >= 0) {
        FirebaseJson &json = data.jsonObject();
        FirebaseJsonData val;
        float mCa = 0, mMg = 0, mAlk = 0, mPh = 0;

        if (json.get(val, "ca"))  mCa  = val.floatValue;
        if (json.get(val, "mg"))  mMg  = val.floatValue;
        if (json.get(val, "alk")) mAlk = val.floatValue;
        if (json.get(val, "ph"))  mPh  = val.floatValue;

        ai.calculateNextPlan(dosingMode, targetAlk - mAlk, targetCa - mCa, targetMg - mMg, mPh, isLightsOn());
        ai.currentPlan.active = true;
        addCurrentAiPlanToBuckets("CloudManual", true);
        publishAiPlanIfNeeded("CloudManual", true);
        Serial.println("Dashboard Manual Entry Detected: AI Plan Adjusted.");
        logger.println("Dashboard Manual Entry Detected: AI Plan Adjusted.");
    }

    if (data.dataPath() == "/config/apex") {
        FirebaseJson &json = data.jsonObject();
        FirebaseJsonData res;
        if (json.get(res, "ip")) apex.setIpAddr(res.stringValue);
        if (json.get(res, "enabled")) apexEnabled = res.boolValue;
    }

    if (data.dataPath() == "/settings/dosingMode") {
        int cloudMode = data.intData();
        if (isValidDosingMode(cloudMode)) {
            dosingMode = cloudMode;
            prefs.begin("doser-settings", false);
            prefs.putInt("dosing_mode", dosingMode);
            prefs.end();
            Serial.printf("Cloud dosingMode applied locally: %d\n", dosingMode);
            logger.printf("Cloud dosingMode applied locally: %d\n", dosingMode);
        }
    }
}

void streamTimeoutCallback(bool timeout) {
    if (timeout) Serial.println("Stream timed out, resuming...");
}

void saveManualTestLocally(float alk, float ca, float mg, float ph) {
    prefs.begin("doser-truth", false);
    prefs.putFloat("last_alk", alk);
    prefs.putFloat("last_ca", ca);
    prefs.putFloat("last_mg", mg);
    prefs.putFloat("last_ph", ph);
    prefs.putULong("last_ts", millis() / 1000UL);
    prefs.end();

    lastLocalTest.alk = alk;
    lastLocalTest.ca = ca;
    lastLocalTest.mg = mg;
    lastLocalTest.ph = ph;
}

void loadManualTestLocally() {
    prefs.begin("doser-truth", true);

    unsigned long lastTs = prefs.getULong("last_ts", 0);

    lastLocalTest.alk = prefs.getFloat("last_alk", 0.0f);
    lastLocalTest.ca  = prefs.getFloat("last_ca", 0.0f);
    lastLocalTest.mg  = prefs.getFloat("last_mg", 0.0f);
    lastLocalTest.ph  = prefs.getFloat("last_ph", 0.0f);
    prefs.end();

    hasSavedManualTest =
        lastTs > 0 &&
        lastLocalTest.alk > 0.0f &&
        lastLocalTest.ca  > 0.0f &&
        lastLocalTest.mg  > 0.0f &&
        lastLocalTest.ph  > 0.0f;

    if (hasSavedManualTest) {
        currentAlk = lastLocalTest.alk;
        currentCa  = lastLocalTest.ca;
        currentMg  = lastLocalTest.mg;
        currentPh  = lastLocalTest.ph;

        Serial.printf("Manual test restored from memory: Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f\n",
                      currentAlk, currentCa, currentMg, currentPh);
        logger.printf("Manual test restored from memory: Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f\n",
                         currentAlk, currentCa, currentMg, currentPh);
    } else {
        Serial.println("No valid saved manual test found yet; live stats will wait for manual/Apex values.");
        logger.println("No valid saved manual test found yet; live stats will wait for manual/Apex values.");
    }
}

void loadFlowRates() {
    prefs.begin("doser-settings", true);
    for (int i = 0; i < 4; ++i) {
        pumpFlowRates[i] = prefs.getFloat(flowPrefKeyForIndex(i), pumpFlowRates[i]);
        if (!isfinite(pumpFlowRates[i]) || pumpFlowRates[i] <= 0.0f) {
            pumpFlowRates[i] = 50.0f;
        }
    }
    prefs.end();
}

void saveFlowRate(int idx, float flowMlPerMin) {
    if (idx < 0 || idx > 3) return;
    pumpFlowRates[idx] = flowMlPerMin;
    prefs.begin("doser-settings", false);
    prefs.putFloat(flowPrefKeyForIndex(idx), flowMlPerMin);
    prefs.end();

    // Keep the running Doser object in sync immediately; otherwise the saved
    // value would not affect live-dose timing until a reboot.
    doser.setCalibration(idx, flowMlPerMin);
}

void loadLocalSettings() {
    prefs.begin("doser-settings", true);
    String savedIp = prefs.getString("apex_ip", "");
    apexEnabled = prefs.getBool("apex_en", false);
    systemMode = prefs.getInt("system_mode", 1);
    dosingMode = prefs.getInt("dosing_mode", 1);
    DOSING_THRESHOLD = prefs.getFloat("d_thresh", 1.0f);
    maxDoseLimit = prefs.getFloat("d_max", 15.0f);
    TANK_VOLUME_L = prefs.getFloat("t_vol", 1135.6f);
    lightConfig.source = prefs.getInt("l_src", lightConfig.source);
    lightConfig.start = prefs.getInt("l_start", lightConfig.start);
    lightConfig.end = prefs.getInt("l_end", lightConfig.end);
    lightConfig.outlet = prefs.getString("l_out", lightConfig.outlet);
    baselineKalkMlDay  = prefs.getFloat("base_kalk", baselineKalkMlDay);
    baselineCacl2MlDay = prefs.getFloat("base_cacl2", baselineCacl2MlDay);
    baselineNaohMlDay  = prefs.getFloat("base_naoh", baselineNaohMlDay);
    baselineMgMlDay    = prefs.getFloat("base_mg", baselineMgMlDay);
    baselineCoralLoad  = prefs.getString("base_load", baselineCoralLoad);
    prefs.end();

    ai.setTankVolumeGallons(TANK_VOLUME_L / 3.78541f);
    applyAiBaselineToEngine();

    if (!isValidSystemMode(systemMode)) systemMode = 1;
    if (!isValidDosingMode(dosingMode)) dosingMode = 1;
    apexIp = savedIp;
    if (savedIp.length() > 0) {
        apex.setIpAddr(savedIp);
    }

    loadFlowRates();
    loadManualTestLocally();
}

bool publishTankVolumeToFirebase(const char* source) {
    if (WiFi.status() != WL_CONNECTED || !firebaseStarted || !Firebase.ready()) {
        return false;
    }

    float tankGallons = TANK_VOLUME_L / 3.78541f;
    tankGallons = roundf(tankGallons);
    String basePath = "/devices/" + deviceID;

    bool okSettings = Firebase.setFloat(writeFbdo, (basePath + "/settings/tankSize").c_str(), tankGallons);
    bool okGallons  = Firebase.setFloat(writeFbdo, (basePath + "/state/tankGallons").c_str(), tankGallons);
    bool okLiters   = Firebase.setFloat(writeFbdo, (basePath + "/state/tankLiters").c_str(), TANK_VOLUME_L);

    if (okSettings && okGallons && okLiters) {
        Serial.printf("Firebase volume pushed [%s]: %.1f gal / %.1f L\n", source ? source : "Volume", tankGallons, TANK_VOLUME_L);
        logger.printf("Firebase volume pushed [%s]: %.1f gal / %.1f L\n", source ? source : "Volume", tankGallons, TANK_VOLUME_L);
        return true;
    }

    Serial.printf("Firebase volume push failed [%s]: %s\n", source ? source : "Volume", writeFbdo.errorReason().c_str());
    logger.printf("Firebase volume push failed [%s]: %s\n", source ? source : "Volume", writeFbdo.errorReason().c_str());
    return false;
}

void mirrorStatusToFirebase() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    // Apex can update the local dashboard before Firebase has finished its delayed startup.
    // Do not silently attempt writes until Firebase is actually ready.
    if (!firebaseStarted || !Firebase.ready()) {
        static unsigned long lastNotReadyLog = 0;
        if (millis() - lastNotReadyLog > 30000UL) {
            lastNotReadyLog = millis();
            Serial.println("Firebase mirror skipped: Firebase not ready yet.");
            logger.println("Firebase mirror skipped: Firebase not ready yet.");
        }
        return;
    }

    FirebaseJson stateJson;
    stateJson.set("online", true);
    stateJson.set("fwVersion", FW_VERSION);
    stateJson.set("tempF", currentTempF);
    stateJson.set("cond", currentCond);
    stateJson.set("ph", currentPh);
    stateJson.set("ppt", currentPpt);
    stateJson.set("sg", currentSg);
    stateJson.set("alk", currentAlk);
    stateJson.set("ca", currentCa);
    stateJson.set("mg", currentMg);
    stateJson.set("emergencyStop", emergencyStop);
    stateJson.set("systemMode", systemMode);
    stateJson.set("dosingMode", dosingMode);
    // Preserve legacy flow keys and add mode-6 aliases so Firebase reflects local status better.
    stateJson.set("flowMlPerMin/kalk", pumpFlowRates[0]);
    stateJson.set("flowMlPerMin/afr", pumpFlowRates[1]);
    stateJson.set("flowMlPerMin/mg", pumpFlowRates[2]);
    stateJson.set("flowMlPerMin/tbd", pumpFlowRates[3]);
    stateJson.set("flowMlPerMin/alk", pumpFlowRates[0]);
    stateJson.set("flowMlPerMin/ca", pumpFlowRates[1]);
    stateJson.set("flowMlPerMin/cacl2", pumpFlowRates[1]);
    stateJson.set("flowMlPerMin/naoh", pumpFlowRates[2]);

    String path = "/devices/" + deviceID + "/state";
    if (Firebase.updateNode(writeFbdo, path.c_str(), stateJson)) {
        static unsigned long lastOkLog = 0;
        if (millis() - lastOkLog > 60000UL) {
            lastOkLog = millis();
            Serial.println("Firebase mirror OK: Apex/state pushed to RTDB.");
            logger.println("Firebase mirror OK: Apex/state pushed to RTDB.");
        }
    } else {
        Serial.printf("Firebase mirror FAILED: %s\n", writeFbdo.errorReason().c_str());
        logger.printf("Firebase mirror FAILED: %s\n", writeFbdo.errorReason().c_str());
    }
}

void handleGetStatus() {
    JsonDocument doc;
    doc["ok"] = true;
    doc["deviceId"] = deviceID;
    doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
    doc["mode"] = systemMode;
    doc["dosingMode"] = dosingMode;
    doc["stop"] = emergencyStop;
    doc["temp"] = currentTempF;
    doc["cond"] = currentCond;
    doc["ph"] = currentPh;
    doc["alk"] = currentAlk;
    doc["ca"] = currentCa;
    doc["mg"] = currentMg;
    doc["ppt"] = currentPpt;
    doc["sg"] = currentSg;
    doc["apexEnabled"] = apexEnabled;
    doc["apexIp"] = apexIp;
    doc["lightConfig"]["source"] = lightConfig.source;
    doc["lightConfig"]["start"] = lightConfig.start;
    doc["lightConfig"]["end"] = lightConfig.end;
    doc["lightConfig"]["outlet"] = lightConfig.outlet;
    doc["aiBaseline"]["kalk"] = baselineKalkMlDay;
    doc["aiBaseline"]["cacl2"] = baselineCacl2MlDay;
    doc["aiBaseline"]["naoh"] = baselineNaohMlDay;
    doc["aiBaseline"]["mg"] = baselineMgMlDay;
    doc["aiBaseline"]["coralLoad"] = baselineCoralLoad;
    doc["dosingMlPerDay"]["kalk"] = ai.currentPlan.kalk;
    doc["dosingMlPerDay"]["afr"] = ai.currentPlan.afr;
    doc["dosingMlPerDay"]["alk"] = ai.currentPlan.alk;
    doc["dosingMlPerDay"]["cacl2"] = ai.currentPlan.cacl2;
    doc["dosingMlPerDay"]["naoh"] = ai.currentPlan.naoh;
    doc["dosingMlPerDay"]["mg"] = ai.currentPlan.mg;
    doc["tankLiters"] = TANK_VOLUME_L;
    doc["tankGallons"] = TANK_VOLUME_L / 3.78541f;

    // Report both physical pump flows and the active chemical mapping.
    // This keeps the dashboard correct when Mg moves from Pump 3 to Pump 4 in modes 5/6.
    doc["flowMlPerMin"]["p1"] = pumpFlowRates[0];
    doc["flowMlPerMin"]["p2"] = pumpFlowRates[1];
    doc["flowMlPerMin"]["p3"] = pumpFlowRates[2];
    doc["flowMlPerMin"]["p4"] = pumpFlowRates[3];
    for (int i = 0; i < 4; ++i) {
        const char* key = pumpKeyForPhysicalIndex(i);
        if (strcmp(key, "unused") != 0) {
            doc["flowMlPerMin"][key] = pumpFlowRates[i];
        }
    }
    // Backward-compatible generic aliases for older dashboard code.
    doc["flowMlPerMin"]["tbd"] = pumpFlowRates[3];

    doc["buckets"]["kalk"] = kalkBucket;
    doc["buckets"]["alk"] = alkBucket;
    doc["buckets"]["ca"] = caBucket;
    doc["buckets"]["mg"] = mgBucket;

    doc["dosingThreshold"] = DOSING_THRESHOLD;
    doc["maxHourlyLimit"] = maxDoseLimit;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handlePostVolume() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));

    float newVol = doc["volume"] | 1135.6f;

    // Save to global and NVS
    TANK_VOLUME_L = newVol;
    prefs.begin("doser-settings", false);
    prefs.putFloat("t_vol", TANK_VOLUME_L);
    prefs.end();

    Serial.printf("AI Geometry Updated: Volume = %.1f L\n", TANK_VOLUME_L);
    logger.printf("AI Geometry Updated: Volume = %.1f L\n", TANK_VOLUME_L);
    
    // Immediately tell the AI engine about the new size
    ai.setTankVolumeGallons(TANK_VOLUME_L / 3.78541f);

    // Push volume only when it changes. If Firebase is not ready yet,
    // the one-shot boot publisher below will publish the latest saved value.
    if (publishTankVolumeToFirebase("VolumeChange")) {
        tankVolumePublishedThisBoot = true;
    } else {
        tankVolumePublishedThisBoot = false;
        Serial.println("Firebase volume push deferred: Firebase not ready yet.");
        logger.println("Firebase volume push deferred: Firebase not ready yet.");
    }

    server.send(200, "application/json", "{\"ok\":true}");
}

void handleGetMode() {
    JsonDocument doc;
    doc["mode"] = systemMode;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handlePostMode() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    int newMode = doc["mode"] | 1;
    if (!isValidSystemMode(newMode)) newMode = 1;

    systemMode = newMode;
    prefs.begin("doser-settings", false);
    prefs.putInt("system_mode", systemMode);
    prefs.end();

    Serial.print("Local operating mode changed to: ");
    Serial.println(systemMode == 0 ? "OFF" : systemMode == 1 ? "AUTO" : "MAN");
    logger.print("Local operating mode changed to: ");
    logger.println(systemMode == 0 ? "OFF" : systemMode == 1 ? "AUTO" : "MAN");

    server.send(200, "application/json", "{\"ok\":true}");
}

void handleGetDosingMode() {
    JsonDocument doc;
    doc["dosingMode"] = dosingMode;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handlePostDosingMode() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    int newMode = doc["dosingMode"] | (doc["mode"] | 1);
    if (!isValidDosingMode(newMode)) newMode = 1;

    dosingMode = newMode;
    prefs.begin("doser-settings", false);
    prefs.putInt("dosing_mode", dosingMode);
    prefs.end();

    if (WiFi.status() == WL_CONNECTED) {
        Firebase.setInt(writeFbdo, ("/devices/" + deviceID + "/settings/dosingMode").c_str(), dosingMode);
        Firebase.setInt(writeFbdo, ("/devices/" + deviceID + "/state/dosingMode").c_str(), dosingMode);
    }

    Serial.printf("Local dosing implementation changed to: %d\n", dosingMode);
    logger.printf("Local dosing implementation changed to: %d\n", dosingMode);
    server.send(200, "application/json", "{\"ok\":true}");
}

void handlePostApexLocal() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    apexEnabled = doc["enabled"] | false;
    String ip = doc["ip"] | "";

    prefs.begin("doser-settings", false);
    prefs.putBool("apex_en", apexEnabled);
    if (ip.length() > 0) {
        prefs.putString("apex_ip", ip);
        apexIp = ip;
        apex.setIpAddr(ip);
    }
    prefs.end();

    Serial.println("Local Apex Config Saved: " + ip);
    logger.println("Local Apex Config Saved: " + ip);
    if (apexEnabled && ip.length() > 7) {
        syncAllTruths();
    }

    server.send(200, "application/json", "{\"ok\":true}");
}

void handlePostCalibration() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    int pumpIndex = doc["pumpIndex"] | -1;
    float flowMlPerMin = doc["flowMlPerMin"] | 0.0f;

    if (pumpIndex < 0 || pumpIndex > 3 || flowMlPerMin <= 0.0f) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid pumpIndex or flowMlPerMin\"}");
        return;
    }

    saveFlowRate(pumpIndex, flowMlPerMin);

    if (WiFi.status() == WL_CONNECTED) {
        String key = pumpKeyForPhysicalIndex(pumpIndex);
        Firebase.setFloat(writeFbdo, ("/devices/" + deviceID + "/state/flowMlPerMin/" + key).c_str(), flowMlPerMin);
    }

    Serial.printf("Saved local flow calibration: pump %d = %.2f ml/min\n", pumpIndex + 1, flowMlPerMin);
    logger.printf("Saved local flow calibration: pump %d = %.2f ml/min\n", pumpIndex + 1, flowMlPerMin);

    JsonDocument out;
    out["ok"] = true;
    out["pumpIndex"] = pumpIndex;
    out["pump"] = pumpKeyForPhysicalIndex(pumpIndex);
    out["flowMlPerMin"] = flowMlPerMin;
    String response;
    serializeJson(out, response);
    server.send(200, "application/json", response);
}


void handlePostCalibrationRun() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }
    if (emergencyStop) {
        server.send(423, "application/json", "{\"ok\":false,\"error\":\"Emergency stop is active\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    int pumpIndex = doc["pumpIndex"] | (doc["pump"] | -1);
    float seconds = doc["seconds"] | 0.0f;

    if (pumpIndex < 0 || pumpIndex >= pumpCountForCurrentDosingMode() || seconds <= 0.0f || seconds > 300.0f) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid pumpIndex or seconds\"}");
        return;
    }

    doser.stopManualRun(pumpIndex);
    doser.startManualRun(pumpIndex);
    calibrationRunActive[pumpIndex] = true;
    calibrationRunUntilMs[pumpIndex] = millis() + (unsigned long)(seconds * 1000.0f);

    Serial.printf("Calibration timed run: pump %d for %.1f seconds\n", pumpIndex + 1, seconds);
    logger.printf("Calibration timed run: pump %d for %.1f seconds\n", pumpIndex + 1, seconds);

    JsonDocument out;
    out["ok"] = true;
    out["pumpIndex"] = pumpIndex;
    out["seconds"] = seconds;
    String response;
    serializeJson(out, response);
    server.send(200, "application/json", response);
}

void handlePostLightConfig() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    // Update your global lightConfig struct
    lightConfig.source = doc["source"] | 0;
    lightConfig.start = doc["start"] | 8;
    lightConfig.end = doc["end"] | 20;
    lightConfig.outlet = doc["outlet"].as<String>();

    // Save to Preferences so it stays after a reboot
    prefs.begin("doser-settings", false);
    prefs.putInt("l_src", lightConfig.source);
    prefs.putInt("l_start", lightConfig.start);
    prefs.putInt("l_end", lightConfig.end);
    prefs.putString("l_out", lightConfig.outlet);
    prefs.end();

    Serial.printf("AI Light Sync: Source=%d, Start=%d, End=%d\n", 
                  lightConfig.source, lightConfig.start, lightConfig.end);
    logger.printf("AI Light Sync: Source=%d, Start=%d, End=%d\n", 
                  lightConfig.source, lightConfig.start, lightConfig.end);

    server.send(200, "application/json", "{\"ok\":true}");
}

// Call this every time you get new sensor data (e.g., in syncAllTruths)
void updateDailyAverages() {
    // Only record real chemistry samples. Do not let reboot/reconnect defaults
    // pollute daily averages with Alk=0.00 / pH=0.00.
    if (currentAlk > 0.0f && currentPh > 0.0f) {
        dailyStats.tempSum += currentTempF;
        dailyStats.phSum += currentPh;
        dailyStats.alkSum += currentAlk;
        dailyStats.count++;
    }
}

void handlePostLiveDose() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }
    if (emergencyStop) {
        server.send(423, "application/json", "{\"ok\":false,\"error\":\"Emergency stop is active\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    int pumpIndex = doc["pumpIndex"] | (doc["pump"] | -1);
    float ml = doc["ml"] | 0.0f;

    if (pumpIndex < 0 || pumpIndex >= pumpCountForCurrentDosingMode() || ml <= 0.0f) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid pumpIndex or ml\"}");
        return;
    }

    doser.doseMl(pumpIndex, ml);
    Serial.printf("Local live dose: physical pump %d, %.2f ml\n", pumpIndex + 1, ml);
    logger.printf("Local live dose: physical pump %d, %.2f ml\n", pumpIndex + 1, ml);
     server.send(200, "application/json", "{\"ok\":true}");
}

void handlePostEmergencyStop() {
    emergencyStop = !emergencyStop;
    if (emergencyStop) {
        doser.stopAllPumps();
        Serial.println("Emergency stop ENABLED from local dashboard");
        logger.println("Emergency stop ENABLED from local dashboard");
    } else {
        Serial.println("Emergency stop CLEARED from local dashboard");
        logger.println("Emergency stop CLEARED from local dashboard");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Firebase.setBool(writeFbdo, ("/devices/" + deviceID + "/state/emergencyStop").c_str(), emergencyStop);
    }

    server.send(200, "application/json", "{\"ok\":true}");
}

void handlePostResetWifi() {
    prefs.begin("doser-settings", false);
    prefs.remove("ssid");
    prefs.remove("pass");
    prefs.end();

    server.send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
    delay(400);
    ESP.restart();
}


void handlePostAiBaseline() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    float kalk  = doc["kalk"]  | baselineKalkMlDay;
    float cacl2 = doc["cacl2"] | baselineCacl2MlDay;
    float naoh  = doc["naoh"]  | baselineNaohMlDay;
    float mg    = doc["mg"]    | baselineMgMlDay;
    String load = doc["coralLoad"] | baselineCoralLoad;

    if (!isfinite(kalk) || kalk < 0.0f || kalk > 100000.0f ||
        !isfinite(cacl2) || cacl2 < 0.0f || cacl2 > 10000.0f ||
        !isfinite(naoh) || naoh < 0.0f || naoh > 10000.0f ||
        !isfinite(mg) || mg < 0.0f || mg > 10000.0f) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid baseline values\"}");
        return;
    }

    baselineKalkMlDay = kalk;
    baselineCacl2MlDay = cacl2;
    baselineNaohMlDay = naoh;
    baselineMgMlDay = mg;
    baselineCoralLoad = load.length() ? load : "custom";

    prefs.begin("doser-settings", false);
    prefs.putFloat("base_kalk", baselineKalkMlDay);
    prefs.putFloat("base_cacl2", baselineCacl2MlDay);
    prefs.putFloat("base_naoh", baselineNaohMlDay);
    prefs.putFloat("base_mg", baselineMgMlDay);
    prefs.putString("base_load", baselineCoralLoad);
    prefs.end();

    applyAiBaselineToEngine();

    Serial.printf("AI Baseline Updated: Kalk=%.2f Cacl2=%.2f NaOH=%.2f Mg=%.2f load=%s\n",
                  baselineKalkMlDay, baselineCacl2MlDay, baselineNaohMlDay, baselineMgMlDay, baselineCoralLoad.c_str());
    logger.printf("AI Baseline Updated: Kalk=%.2f Cacl2=%.2f NaOH=%.2f Mg=%.2f load=%s\n",
                     baselineKalkMlDay, baselineCacl2MlDay, baselineNaohMlDay, baselineMgMlDay, baselineCoralLoad.c_str());

    server.send(200, "application/json", "{\"ok\":true}");
}

void handlePostDosingSafeties() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));

    DOSING_THRESHOLD = doc["threshold"] | 1.0f;
    maxDoseLimit = doc["maxLimit"] | 15.0f;

    // Save to Preferences
    prefs.begin("doser-settings", false);
    prefs.putFloat("d_thresh", DOSING_THRESHOLD);
    prefs.putFloat("d_max", maxDoseLimit);
    prefs.end();

    Serial.printf("Safeties Updated: Threshold %.2f, Max %.2f\n", DOSING_THRESHOLD, maxDoseLimit);
    logger.printf("Safeties Updated: Threshold %.2f, Max %.2f\n", DOSING_THRESHOLD, maxDoseLimit);
    server.send(200, "application/json", "{\"ok\":true}");
}

// 3. The "Trigger" logic (run when Apex data arrives)
void onNewDataArrived(float rawAlk) {
    alkHistory[alkIdx] = rawAlk;
    alkIdx = (alkIdx + 1) % 5;

    float sum = 0;
    int count = 0;
    for(int i=0; i<5; i++) {
        if(alkHistory[i] > 0) {
            sum += alkHistory[i];
            count++;
        }
    }
    float smoothedAlk = (count > 0) ? (sum / count) : rawAlk;
    bool lightState = isLightsOn();

    // Fixed: systemMode first, then the 3 dosing floats, then pH, then light bool
    ai.setTankVolumeGallons(TANK_VOLUME_L / 3.78541f);
    applyAiBaselineToEngine();
    ai.calculateNextPlan(dosingMode, targetAlk - smoothedAlk, targetCa - currentCa, targetMg - currentMg, currentPh, isLightsOn());
    ai.currentPlan.active = true;
    addCurrentAiPlanToBuckets("Apex", false);
    publishAiPlanIfNeeded("Apex", false);
}


void syncAllTruths() {
    String response = apex.getState();
    if (response.length() > 0) {
        currentTempF = apex.getTempF();
        currentPh    = apex.getPh();
        currentAlk   = apex.getAlk();
        currentCa    = apex.getCa();
        currentMg    = apex.getMg();
        currentCond  = apex.getCond();

        // Recalculate AI after all Apex values are current, then add the plan into buckets.
        onNewDataArrived(currentAlk);

        // Apex status.json Cond/Salt value is already salinity in PPT for this probe.
        // Do NOT multiply by 0.67 here; 37.0 from Apex should display as 37.0 PPT,
        // not 24.8. Keep currentCond as the raw Apex value and mirror PPT from it.
        currentPpt   = currentCond;

        // Approximate SG from PPT: 35 ppt ~= 1.0264, so 1 ppt ~= 0.000754 SG.
        currentSg    = 1.000f + (currentPpt * 0.000754f);
    }

    // Local variables above update immediately for the dashboard.
    // Firebase RTDB mirror is throttled to once every 5 minutes.
    if (millis() - lastFirebaseMirrorMs >= FIREBASE_MIRROR_INTERVAL_MS) {
        lastFirebaseMirrorMs = millis();
        mirrorStatusToFirebase();
    }
    
}





void publishFirmwareVersionIfReady(bool force) {
    if (WiFi.status() != WL_CONNECTED || !firebaseStarted || !Firebase.ready()) {
        return;
    }

    unsigned long nowMs = millis();
    if (!force && fwVersionPublished) {
        return;
    }
    if (!force && lastFwVersionPublishAttemptMs != 0 &&
        (nowMs - lastFwVersionPublishAttemptMs) < 5000UL) {
        return;
    }
    lastFwVersionPublishAttemptMs = nowMs;

    String fwPath = "/devices/" + deviceID + "/state/fwVersion";
    String onlinePath = "/devices/" + deviceID + "/state/online";

    bool fwOk = Firebase.setString(writeFbdo, fwPath.c_str(), FW_VERSION);
    if (!fwOk) {
        Serial.printf("FW VERSION PUSH FAILED: %s\n", writeFbdo.errorReason().c_str());
        logger.printf("FW VERSION PUSH FAILED: %s\n", writeFbdo.errorReason().c_str());
        return;
    }

    Firebase.setBool(writeFbdo, onlinePath.c_str(), true);

    fwVersionPublished = true;
    Serial.printf("FW VERSION PUSHED: %s to %s\n", FW_VERSION, fwPath.c_str());
    logger.printf("FW VERSION PUSHED: %s to %s\n", FW_VERSION, fwPath.c_str());
    WebSerial.printf("FW VERSION PUSHED: %s\n", FW_VERSION);
}

void connectToFirebase() {
    config.api_key = FIREBASE_API_KEY;
    config.database_url = FIREBASE_DB_URL;
    config.token_status_callback = tokenStatusCallback;

    if (Firebase.signUp(&config, &auth, auth.user.email.c_str(), auth.user.password.c_str())) {
        Serial.printf("Firebase Auth Success for %s\n", deviceID.c_str());
        logger.printf("Firebase Auth Success for %s\n", deviceID.c_str());
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);

        // Try now, then loop() will retry once Firebase.ready() is true.
        publishFirmwareVersionIfReady(true);
        
        String path = "/devices/" + deviceID + "/commands";
        if (!Firebase.beginStream(streamFbdo, path.c_str())) {
            Serial.printf("Stream error: %s\n", streamFbdo.errorReason().c_str());
            logger.printf("Stream error: %s\n", streamFbdo.errorReason().c_str());
        }
        Firebase.setStreamCallback(streamFbdo, streamCallback, streamTimeoutCallback);
    } else {
        Serial.printf("Firebase Auth Failed: %s\n", config.signer.signupError.message.c_str());
        logger.printf("Firebase Auth Failed: %s\n", config.signer.signupError.message.c_str());
    }
}

void handleRoot() {
    server.send_P(200, "text/html", kIndexHtml);
}

void pushDailyReport() {
    if (dailyStats.count == 0) {
        Serial.println("Midnight report skipped: no daily samples yet.");
        logger.println("Midnight report skipped: no daily samples yet.");
        return;
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    char monthFolder[8]; // YYYY-MM
    char dayFolder[3];   // DD
    strftime(monthFolder, sizeof(monthFolder), "%Y-%m", &timeinfo);
    strftime(dayFolder, sizeof(dayFolder), "%d", &timeinfo);

    float count = (float)dailyStats.count;
    float avgPh   = dailyStats.phSum / count;
    float avgTemp = dailyStats.tempSum / count;
    float avgAlk  = dailyStats.alkSum / count;
    float avgCa   = (dailyStats.caSum  > 0.0f) ? (dailyStats.caSum  / count) : currentCa;
    float avgMg   = (dailyStats.mgSum  > 0.0f) ? (dailyStats.mgSum  / count) : currentMg;
    float avgPpt  = (dailyStats.pptSum > 0.0f) ? (dailyStats.pptSum / count) : currentPpt;
    float avgSg   = (dailyStats.sgSum  > 0.0f) ? (dailyStats.sgSum  / count) : currentSg;
    float totalDose = dailyDoseTotals[0] + dailyDoseTotals[1] + dailyDoseTotals[2] + dailyDoseTotals[3];

    FirebaseJson json;

    // One low-cost daily history record for graphs and reports.
    json.set("params/ph", avgPh);
    json.set("params/temp", avgTemp);
    json.set("params/tempF", avgTemp);
    json.set("params/alk", avgAlk);
    json.set("params/ca", avgCa);
    json.set("params/mg", avgMg);
    json.set("params/ppt", avgPpt);
    json.set("params/sg", avgSg);
    json.set("params/totalDose", totalDose);
    json.set("sampleCount", dailyStats.count);
    json.set("timestamp", (uint32_t)(millis() / 1000UL));

    // Always keep physical-pump totals, and also include the active chemical aliases.
    json.set("dosing/p1", dailyDoseTotals[0]);
    json.set("dosing/p2", dailyDoseTotals[1]);
    json.set("dosing/p3", dailyDoseTotals[2]);
    json.set("dosing/p4", dailyDoseTotals[3]);
    for (int i = 0; i < 4; ++i) {
        const char* key = pumpKeyForPhysicalIndex(i);
        if (strcmp(key, "unused") != 0) {
            json.set(String("dosing/") + key, dailyDoseTotals[i]);
        }
    }

    // Daily copy of the latest plan, so the cloud UI can show what the controller intended.
    json.set("plan/kalk", ai.currentPlan.kalk);
    json.set("plan/afr", ai.currentPlan.afr);
    json.set("plan/alk", ai.currentPlan.alk);
    json.set("plan/cacl2", ai.currentPlan.cacl2);
    json.set("plan/naoh", ai.currentPlan.naoh);
    json.set("plan/mg", ai.currentPlan.mg);

    // Path: devices/reefDoser1/history/2026-05/15
    String path = "/devices/" + deviceID + "/history/" + String(monthFolder) + "/" + String(dayFolder);

    if (Firebase.updateNode(writeFbdo, path.c_str(), json)) {
        Serial.printf("Midnight daily history pushed: %s/%s totalDose=%.2f ml\n", monthFolder, dayFolder, totalDose);
        logger.printf("Midnight daily history pushed: %s/%s totalDose=%.2f ml\n", monthFolder, dayFolder, totalDose);
    } else {
        Serial.printf("Midnight report push failed: %s\n", writeFbdo.errorReason().c_str());
        logger.printf("Midnight report push failed: %s\n", writeFbdo.errorReason().c_str());
        return; // Keep totals in NVS and try again in the midnight window.
    }

    memset(&dailyStats, 0, sizeof(dailyStats));
    for (int i = 0; i < 4; i++) dailyDoseTotals[i] = 0.0f;

    saveDosingState();
    Serial.println("Midnight: daily stats/totals cleared and saved.");
    logger.println("Midnight: daily stats/totals cleared and saved.");
}

void handlePostManualTest() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    float alk = doc["alk"] | 0.0f;
    float ca  = doc["ca"]  | 0.0f;
    float mg  = doc["mg"]  | 0.0f;
    float ph  = doc["ph"]  | 0.0f;

    ai.calculateNextPlan(dosingMode, targetAlk - alk, targetCa - ca, targetMg - mg, ph, isLightsOn());
    ai.currentPlan.active = true;
    addCurrentAiPlanToBuckets("Manual", true);
    publishAiPlanIfNeeded("Manual", true);
    saveManualTestLocally(alk, ca, mg, ph);

    currentAlk = alk;
    currentCa = ca;
    currentMg = mg;
    currentPh = ph;

    syncAllTruths();
    server.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
    Serial.begin(115200);
    
    delay(500);
LittleFS.begin(true);
//LittleFS.format();
    esp_reset_reason_t reason = esp_reset_reason();
/*Serial.println("FORMATTING LITTLEFS LOG STORAGE NOW...");
WebSerial.println("FORMATTING LITTLEFS LOG STORAGE NOW...");
LittleFS.format();
Serial.println("LITTLEFS FORMAT DONE.");
WebSerial.println("LITTLEFS FORMAT DONE.");
esp_reset_reason_t reason = esp_reset_reason();*/



    // Register WebSerial early so any later WebSerial prints are safe.
    WebSerial.begin(&serialServer);
    logger.begin(deviceID, GOOGLE_LOG_URL);
    Serial.println("BOOT CHECK DEVICE ID = " + deviceID);
WebSerial.println("BOOT CHECK DEVICE ID = " + deviceID);
logger.println("BOOT CHECK DEVICE ID = " + deviceID);
//LittleFS.format();
    //LittleFS.remove("/logs/current.log");
/////////////////////////////
/*File root = LittleFS.open("/logs");
if (root && root.isDirectory()) {
  File file = root.openNextFile();
  while (file) {
    String path = file.path();
    file.close();
    if (path.endsWith(".log")) {
      LittleFS.remove(path);
      Serial.println("Removed old log: " + path);
    }
    file = root.openNextFile();
  }
}*/
////////////////////////////
   // LittleFS.remove("/logs/queued_1778883043.log");
    logger.println();
logger.println("================================");
logger.printf("RESET REASON: %s (%d)\n",
              resetReasonToString(reason),
              reason);
logger.println("================================");
logger.println();

    // 120-second watchdog. Firebase SSL stream recovery can block during token/SSL reconnects.
    // Keep watchdog protection, but give network recovery enough time to unwind safely.
    esp_task_wdt_init(120, true);
    esp_task_wdt_add(NULL);

    EEPROM.begin(512);

    prefs.begin("doser-settings", false);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    DOSING_THRESHOLD = prefs.getFloat("d_thresh", 1.0f);
    maxDoseLimit = prefs.getFloat("d_max", 15.0f);
    TANK_VOLUME_L = prefs.getFloat("t_vol", 1135.6f);
    prefs.end();

    loadLocalSettings();

    bool wifiConnected = false;

    if (ssid == "") {
        Serial.println("No WiFi saved. Starting Hotspot...");
        logger.println("No WiFi saved. Starting Hotspot...");
        provisioner.startPortal("ReefDoser_Setup");
        state.transitionTo(SystemState::PROVISIONING);
    } else {
        Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
        logger.printf("Connecting to WiFi: %s\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());

        int retryCount = 0;
        while (WiFi.status() != WL_CONNECTED && retryCount < 20) {
            delay(500);
            Serial.print(".");
            logger.print(".");
            retryCount++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.println("\nWiFi Connected!");
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
            logger.println("\nWiFi Connected!");
            logger.print("IP Address: ");
            logger.println(WiFi.localIP().toString());

            doser.begin();
            state.transitionTo(SystemState::IDLE);
        } else {
            Serial.println("\nWiFi Connection Failed. Reverting to Hotspot...");
            logger.println("\nWiFi Connection Failed. Reverting to Hotspot...");
            provisioner.startPortal("ReefDoser_Setup");
            state.transitionTo(SystemState::PROVISIONING);
        }
    }
    // ---------------- LOCAL DASHBOARD ROUTES ----------------
    server.on("/", HTTP_GET, handleRoot);
    server.on("/ping", HTTP_GET, []() {
        server.send(200, "text/plain", "pong");
    });

    server.on("/api/config/volume", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Body missing");
            return;
        }
        JsonDocument doc;
        deserializeJson(doc, server.arg("plain"));

        // Dashboard sends gallons and volume liters. Accept both to stay backward-compatible.
        if (doc["gallons"].is<float>() || doc["gallons"].is<int>()) {
            TANK_VOLUME_L = (doc["gallons"] | 300.0f) * 3.78541f;
        } else {
            TANK_VOLUME_L = doc["volume"] | 1135.6f;
        }

        prefs.begin("doser-settings", false);
        prefs.putFloat("t_vol", TANK_VOLUME_L);
        prefs.end();

        ai.setTankVolumeGallons(TANK_VOLUME_L / 3.78541f);
        applyAiBaselineToEngine();
        Serial.printf("Tank Volume Updated: %.2f L (%.1f gal)\n", TANK_VOLUME_L, TANK_VOLUME_L / 3.78541f);
        logger.printf("Tank Volume Updated: %.2f L (%.1f gal)\n", TANK_VOLUME_L, TANK_VOLUME_L / 3.78541f);
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/status", HTTP_GET, handleGetStatus);
    server.on("/api/manual-test", HTTP_POST, handlePostManualTest);
    server.on("/api/config/apex", HTTP_POST, handlePostApexLocal);

    server.on("/api/mode", HTTP_GET, handleGetMode);
    server.on("/api/mode", HTTP_POST, handlePostMode);

    server.on("/api/dosing-mode", HTTP_GET, handleGetDosingMode);
    server.on("/api/dosing-mode", HTTP_POST, handlePostDosingMode);

    server.on("/api/calibration", HTTP_POST, handlePostCalibration);
    server.on("/api/calibration-run", HTTP_POST, handlePostCalibrationRun);
    server.on("/api/live-dose", HTTP_POST, handlePostLiveDose);
    server.on("/api/emergency-stop", HTTP_POST, handlePostEmergencyStop);
    server.on("/api/reset-wifi", HTTP_POST, handlePostResetWifi);

    server.on("/api/logger/force-upload", HTTP_POST, []() {
        logger.println("Manual logger upload requested from local API.");
        logger.forceUpload();
        server.send(200, "application/json", "{\"ok\":true,\"message\":\"Logger upload attempted\"}");
    });
    server.on("/api/config/safeties", HTTP_POST, handlePostDosingSafeties);
    server.on("/api/config/ai-baseline", HTTP_POST, handlePostAiBaseline);
    server.on("/api/config/lights", HTTP_POST, handlePostLightConfig);

    server.on("/api/history", HTTP_GET, []() {
        JsonDocument doc;

        JsonArray labels = doc.createNestedArray("labels");
        labels.add("Today");

        JsonObject params = doc.createNestedObject("params");
        JsonObject today = params.createNestedObject("Today");

        float count = (dailyStats.count > 0) ? (float)dailyStats.count : 1.0f;
        float totalDose = dailyDoseTotals[0] + dailyDoseTotals[1] + dailyDoseTotals[2] + dailyDoseTotals[3];

        today["alk"] = (dailyStats.count > 0) ? (dailyStats.alkSum / count) : currentAlk;
        today["ph"] = (dailyStats.count > 0) ? (dailyStats.phSum / count) : currentPh;
        today["temp"] = (dailyStats.count > 0) ? (dailyStats.tempSum / count) : currentTempF;
        today["tempF"] = (dailyStats.count > 0) ? (dailyStats.tempSum / count) : currentTempF;
        today["ca"] = (dailyStats.count > 0 && dailyStats.caSum > 0.0f) ? (dailyStats.caSum / count) : currentCa;
        today["mg"] = (dailyStats.count > 0 && dailyStats.mgSum > 0.0f) ? (dailyStats.mgSum / count) : currentMg;
        today["ppt"] = (dailyStats.count > 0 && dailyStats.pptSum > 0.0f) ? (dailyStats.pptSum / count) : currentPpt;
        today["sal"] = today["ppt"];
        today["sg"] = (dailyStats.count > 0 && dailyStats.sgSum > 0.0f) ? (dailyStats.sgSum / count) : currentSg;
        today["totalDose"] = totalDose;

        JsonObject dosing = doc.createNestedObject("dosing");
        JsonObject doseToday = dosing.createNestedObject("Today");
        doseToday["p1"] = dailyDoseTotals[0];
        doseToday["p2"] = dailyDoseTotals[1];
        doseToday["p3"] = dailyDoseTotals[2];
        doseToday["p4"] = dailyDoseTotals[3];
        doseToday[pumpKeyForPhysicalIndex(0)] = dailyDoseTotals[0];
        doseToday[pumpKeyForPhysicalIndex(1)] = dailyDoseTotals[1];
        doseToday[pumpKeyForPhysicalIndex(2)] = dailyDoseTotals[2];
        doseToday[pumpKeyForPhysicalIndex(3)] = dailyDoseTotals[3];

        doc["ok"] = true;
        doc["dosingMode"] = dosingMode;
        JsonObject planToday = doc.createNestedObject("plan").createNestedObject("Today");
        planToday["kalk"] = ai.currentPlan.kalk;
        planToday["afr"] = ai.currentPlan.afr;
        planToday["alk"] = ai.currentPlan.alk;
        planToday["cacl2"] = ai.currentPlan.cacl2;
        planToday["naoh"] = ai.currentPlan.naoh;
        planToday["mg"] = ai.currentPlan.mg;

        doc["sampleCount"] = dailyStats.count;

        String output;
        serializeJson(doc, output);
        server.send(200, "application/json", output);
    });

    // Start local services BEFORE Firebase so the dashboard remains reachable even if Firebase is slow.
    server.begin();
    Serial.println("Dashboard web server started on port 80");
    logger.println("Dashboard web server started on port 80");

    serialServer.begin();
    Serial.println("WebSerial started on port 81");
    logger.println("WebSerial started on port 81");

    lastSliceMillis = millis();
    configTime(-6 * 3600, 3600, "pool.ntp.org", "time.google.com");
    logger.println("CST Time Sync Initialized...");

    loadDosingState();
    logger.println("Dosing state recovered from memory.");

    // Restore the live AI plan after reboot/OTA so /api/status and the local dashboard
    // do not show a zero dosing plan while saved bucket state still exists.
    if (hasSavedManualTest) {
        ai.calculateNextPlan(
            dosingMode,
            targetAlk - currentAlk,
            targetCa  - currentCa,
            targetMg  - currentMg,
            currentPh,
            isLightsOn()
        );
        ai.currentPlan.active = true;

        Serial.println("AI plan restored from saved manual test.");
        logger.println("AI plan restored from saved manual test.");
    }

    Serial.printf("Boot volume: %.2f L (%.1f gal)\n", TANK_VOLUME_L, TANK_VOLUME_L / 3.78541f);
    logger.printf("Boot volume: %.2f L (%.1f gal)\n", TANK_VOLUME_L, TANK_VOLUME_L / 3.78541f);

    if (wifiConnected) {
        Serial.println("Firebase startup delayed 30 seconds so dashboard can come up first.");
        logger.println("Firebase startup delayed 30 seconds so dashboard can come up first.");
    }
}

void loop() {
    yield();
    esp_task_wdt_reset(); // Tell the system "I'm still alive"
    doser.tick();
    logger.loop();

    // Stop calibration timed runs exactly by elapsed time.
    // This is independent of mL/min calibration and does not affect quick-dose dosing.
    unsigned long calNow = millis();
    for (int i = 0; i < 4; i++) {
        if (calibrationRunActive[i] && (long)(calNow - calibrationRunUntilMs[i]) >= 0) {
            doser.stopManualRun(i);
            calibrationRunActive[i] = false;
            Serial.printf("Calibration timed run complete: pump %d\n", i + 1);
            logger.printf("Calibration timed run complete: pump %d\n", i + 1);
        }
    }

    server.handleClient();

    // Delay Firebase startup so local dashboard can be reached first.
    // This preserves Firebase/OTA/commands but prevents SSL stream startup from starving port 80.
    static unsigned long bootMs = millis();
    if (!firebaseStarted && WiFi.status() == WL_CONNECTED && (millis() - bootMs > 30000UL)) {
        firebaseStarted = true;
        Serial.println("Starting Firebase after dashboard grace period...");
        logger.println("Starting Firebase after dashboard grace period...");
        connectToFirebase();
        if (apexEnabled) {
            syncAllTruths();
        }
    }

    // Publish firmware version as soon as Firebase is truly ready.
    publishFirmwareVersionIfReady(false);

    // Publish tank volume once per boot after Firebase is ready. Do not include
    // this in the recurring state mirror; volume only changes when the user saves it.
    if (!tankVolumePublishedThisBoot && firebaseStarted && WiFi.status() == WL_CONNECTED && Firebase.ready()) {
        unsigned long nowMs = millis();
        if (lastTankVolumePublishAttemptMs == 0 || (nowMs - lastTankVolumePublishAttemptMs) > 30000UL) {
            lastTankVolumePublishAttemptMs = nowMs;
            tankVolumePublishedThisBoot = publishTankVolumeToFirebase("Boot");
        }
    }

    // Keep the dashboard responsive. Firebase SSL stream reads can block on ESP32,
    // especially right after token refresh. Do not immediately reconnect after a
    // stream failure. Instead, mark the stream for a delayed reconnect and keep
    // dosing/logger/local UI alive while SSL cools down.
    // IMPORTANT: Logger and OTA are proven OK. Overnight resets were from
    // Firebase stream SSL recovery hanging long enough to trip the watchdog.
    static unsigned long lastStreamRead = 0;
    static unsigned long lastStreamReconnectAttempt = 0;
    static unsigned long streamBackoffUntilMs = 0;
    static uint8_t streamFailCount = 0;
    static bool streamNeedsReconnect = false;

    const unsigned long STREAM_READ_EVERY_MS = 10000UL;        // less SSL pressure
    const unsigned long STREAM_RECONNECT_EVERY_MS = 300000UL;  // at most once every 5 minutes
    const unsigned long STREAM_BACKOFF_MS = 600000UL;          // 10-minute cooloff after failures

    unsigned long streamNow = millis();

    if (firebaseStarted && WiFi.status() == WL_CONNECTED && Firebase.ready()) {
        // If the stream was intentionally stopped after SSL failures, do not
        // hammer reconnect. Wait for the backoff, then try exactly once.
        if (streamNeedsReconnect) {
            bool backoffDone = (long)(streamNow - streamBackoffUntilMs) >= 0;
            bool reconnectDue = (streamNow - lastStreamReconnectAttempt) >= STREAM_RECONNECT_EVERY_MS;

            if (backoffDone && reconnectDue) {
                lastStreamReconnectAttempt = streamNow;

                Serial.println("STREAM: attempting delayed reconnect...");
                logger.println("STREAM: attempting delayed reconnect...");

                serviceWatchdogAndUi(20);

                String path = "/devices/" + deviceID + "/commands";
                bool streamStarted = Firebase.beginStream(streamFbdo, path.c_str());

                serviceWatchdogAndUi(20);

                if (streamStarted) {
                    Firebase.setStreamCallback(streamFbdo, streamCallback, streamTimeoutCallback);
                    streamNeedsReconnect = false;
                    streamFailCount = 0;
                    lastStreamRead = millis();
                    Serial.println("Stream Restored.");
                    logger.println("Stream Restored.");
                } else {
                    streamBackoffUntilMs = millis() + STREAM_BACKOFF_MS;
                    Serial.printf("Stream delayed reconnect failed: %s\n", streamFbdo.errorReason().c_str());
                    logger.printf("Stream delayed reconnect failed: %s\n", streamFbdo.errorReason().c_str());
                    Serial.println("STREAM: backing off 10 minutes before next reconnect.");
                    logger.println("STREAM: backing off 10 minutes before next reconnect.");
                }
            }
        }
        // Normal stream read path. If it fails, stop reading and back off.
        else if (streamNow - lastStreamRead >= STREAM_READ_EVERY_MS) {
            lastStreamRead = streamNow;

            serviceWatchdogAndUi(5);

            bool streamOk = Firebase.readStream(streamFbdo);

            serviceWatchdogAndUi(5);

            if (streamOk) {
                if (streamFailCount > 0) {
                    streamFailCount = 0;
                    Serial.println("Stream read OK after previous errors.");
                    logger.println("Stream read OK after previous errors.");
                }
            } else {
                streamFailCount++;

                int code = streamFbdo.httpCode();

                static unsigned long lastStreamErrLog = 0;
                if (code != FIREBASE_ERROR_HTTP_CODE_OK &&
                    code != FIREBASE_ERROR_TCP_ERROR_NOT_CONNECTED &&
                    millis() - lastStreamErrLog > 60000UL) {

                    lastStreamErrLog = millis();
                    String err = streamFbdo.errorReason();
                    if (err.length() == 0) err = "blank/temporary stream error";

                    Serial.printf("Stream read error: %s\n", err.c_str());
                    logger.printf("Stream read error: %s\n", err.c_str());
                }

                if (streamFailCount >= 2) {
                    Serial.println("STREAM LOST - entering safe backoff before reconnect.");
                    logger.println("STREAM LOST - entering safe backoff before reconnect.");

                    serviceWatchdogAndUi(20);
                    Firebase.endStream(streamFbdo);
                    serviceWatchdogAndUi(20);

                    streamNeedsReconnect = true;
                    streamFailCount = 0;
                    streamBackoffUntilMs = millis() + STREAM_BACKOFF_MS;
                    lastStreamReconnectAttempt = millis();

                    Serial.println("STREAM: backing off 10 minutes before reconnect.");
                    logger.println("STREAM: backing off 10 minutes before reconnect.");
                }
            }
        }
    }

    if (state.getCurrentState() == SystemState::PROVISIONING) {
        provisioner.handleClient();
        if (provisioner.isConfigurationDone()) {
            ESP.restart();
        }
        return;
    }

    unsigned long now = millis();

    // Diagnostics: each 10-minute dosing window gets one slot ID.
    // The pump queue below keeps one-pump-at-a-time safety, but continues
    // servicing ready buckets after the previous pump finishes instead of
    // skipping them until the next 10-minute slot.
    static unsigned long doseSlotId = 0;
    static unsigned long pumpDumpCount[4] = {0, 0, 0, 0};

    if (now - lastSliceMillis >= 600000UL) {
        lastSliceMillis = now;
            logger.println("\nWiFi Connected!");
            logger.print("IP Address: ");
            logger.println(WiFi.localIP().toString());
        doseSlotId++;

        Serial.printf("[SLOT %lu] checking buckets: P1=%.2f P2=%.2f P3=%.2f P4=%.2f threshold=%.2f\n",
                      doseSlotId, pumpBuckets[0], pumpBuckets[1], pumpBuckets[2], pumpBuckets[3], DOSING_THRESHOLD);
        logger.printf("[SLOT %lu] checking buckets: P1=%.2f P2=%.2f P3=%.2f P4=%.2f threshold=%.2f\n",
                         doseSlotId, pumpBuckets[0], pumpBuckets[1], pumpBuckets[2], pumpBuckets[3], DOSING_THRESHOLD);

        for (int i = 0; i < 4; i++) {
            if (pumpBuckets[i] >= DOSING_THRESHOLD) {
                Serial.printf("[SLOT %lu] PUMP %d queued: bucket %.2f ready to dose.\n",
                              doseSlotId, i + 1, pumpBuckets[i]);
                logger.printf("[SLOT %lu] PUMP %d queued: bucket %.2f ready to dose.\n",
                                 doseSlotId, i + 1, pumpBuckets[i]);
            } else {
                Serial.printf("[SLOT %lu] PUMP %d no dump: bucket %.2f below threshold %.2f\n",
                              doseSlotId, i + 1, pumpBuckets[i], DOSING_THRESHOLD);
                logger.printf("[SLOT %lu] PUMP %d no dump: bucket %.2f below threshold %.2f\n",
                                 doseSlotId, i + 1, pumpBuckets[i], DOSING_THRESHOLD);
            }
        }
    }

    // One-pump-at-a-time bucket service. This runs every loop, so if Pump 1
    // takes 20+ seconds, Pump 2/Pump 4 will start as soon as Pump 1 finishes
    // instead of waiting for the next 10-minute slot.
    bool anyPumpRunning = false;
    for (int i = 0; i < 4; i++) {
        if (doser.isPumpRunning(i)) {
            anyPumpRunning = true;
            break;
        }
    }

    if (!anyPumpRunning && !emergencyStop) {
        for (int i = 0; i < 4; i++) {
            if (pumpBuckets[i] >= DOSING_THRESHOLD) {
                float doseAmount = pumpBuckets[i];

                doser.doseMl(i, doseAmount);
                pumpBuckets[i] = 0.0f;

                saveDosingState();
                pumpDumpCount[i]++;

                Serial.printf("[SLOT %lu] DUMP #%lu PUMP %d: %.2f ml, bucket reset to %.2f\n",
                              doseSlotId, pumpDumpCount[i], i + 1, doseAmount, pumpBuckets[i]);
                logger.printf("[SLOT %lu] DUMP #%lu PUMP %d: %.2f ml, bucket reset to %.2f\n",
                                 doseSlotId, pumpDumpCount[i], i + 1, doseAmount, pumpBuckets[i]);
                break;
            }
        }
    } else if (anyPumpRunning) {
        static unsigned long lastPumpQueueWaitLogMs = 0;
        if (now - lastPumpQueueWaitLogMs >= 5000UL) {
            lastPumpQueueWaitLogMs = now;
            for (int i = 0; i < 4; i++) {
                if (pumpBuckets[i] >= DOSING_THRESHOLD) {
                    Serial.printf("[SLOT %lu] PUMP %d ready to dose %.2f ml, waiting for active pump to finish.\n",
                                  doseSlotId, i + 1, pumpBuckets[i]);
                    logger.printf("[SLOT %lu] PUMP %d ready to dose %.2f ml, waiting for active pump to finish.\n",
                                     doseSlotId, i + 1, pumpBuckets[i]);
                    break;
                }
            }
        }
    }


    static unsigned long lastAIUpdate = 0;
    if (now - lastAIUpdate >= 3600000UL) {
        lastAIUpdate = now;
        bool lightState = isLightsOn();
        Serial.print("light state is  ");
        Serial.println(lightState);
        logger.print("light state is  ");
        logger.println(lightState ? "true" : "false");
        // Ensure AI has the latest volume before calculating
        ai.setTankVolumeGallons(TANK_VOLUME_L / 3.78541f); 
        applyAiBaselineToEngine();
    
        Serial.printf("[AI] Calculating plan for %.1fL volume...\n", TANK_VOLUME_L);
        logger.printf("[AI] Calculating plan for %.1fL volume...\n", TANK_VOLUME_L);
        ai.calculateNextPlan(dosingMode, targetAlk - lastLocalTest.alk, targetCa - lastLocalTest.ca, targetMg - lastLocalTest.mg, lastLocalTest.ph, isLightsOn());
        ai.currentPlan.active = true;
        addCurrentAiPlanToBuckets("Hourly", false);
        publishAiPlanIfNeeded("Hourly", false);
    }

    static unsigned long lastApexPull = 0;
    if (apexEnabled && (millis() - lastApexPull > 300000UL)) {
        lastApexPull = millis();
        syncAllTruths();
    }

    if (currentTempF > 84.0f || currentPh > 11.0f) {
        if (!emergencyStop) {
            emergencyStop = true;
            doser.stopAllPumps();
            Serial.println("SAFETY TRIPPED: Local sensors exceeded limits!");
            logger.println("SAFETY TRIPPED: Local sensors exceeded limits!");
        }
    }

    // 1. UPDATE AVERAGES (Every 5 mins)
    static unsigned long lastAvgUpdate = 0;
    if (millis() - lastAvgUpdate > 300000UL) {
        lastAvgUpdate = millis();

        if (currentAlk > 0.0f && currentPh > 0.0f) {
            dailyStats.tempSum += currentTempF;
            dailyStats.phSum += currentPh;
            dailyStats.alkSum += currentAlk;
            if (currentCa > 0.0f) dailyStats.caSum += currentCa;
            if (currentMg > 0.0f) dailyStats.mgSum += currentMg;
            if (currentPpt > 0.0f) dailyStats.pptSum += currentPpt;
            if (currentSg > 0.0f) dailyStats.sgSum += currentSg;
            dailyStats.count++;

            saveDosingState(); // <--- SAVE HERE
            // ADD THIS LOG:  //TODO remove this in prodcution
            Serial.printf("[STATS] Sample #%d recorded. Current Alk: %.2f, pH: %.2f\n",
                          dailyStats.count, currentAlk, currentPh);
            logger.printf("[STATS] Sample #%d recorded. Current Alk: %.2f, pH: %.2f\n",
                             dailyStats.count, currentAlk, currentPh);
        } else {
            Serial.println("[STATS] skipped: no valid Alk/pH yet");
            logger.println("[STATS] skipped: no valid Alk/pH yet");
        }
    }

    // 2. MIDNIGHT PUSH
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        // Use a 10-minute window so a busy SSL/Apex cycle does not miss the midnight report.
        if (timeinfo.tm_hour == 0 && timeinfo.tm_min < 10) {
            if (!reportPushedToday) {
                if (firebaseStarted && Firebase.ready()) {
                    pushDailyReport();
                    reportPushedToday = true;
                } else {
                    Serial.println("Midnight push waiting for Firebase...");
                    logger.println("Midnight push waiting for Firebase...");
                }
            }
        } else {
            reportPushedToday = false; // Reset flag for next day
        }
    }
}
