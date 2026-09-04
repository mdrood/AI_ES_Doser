#include <Arduino.h>

// 1. SYSTEM LIBRARIES (MUST BE FIRST)
#include <FS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include "global_config.h"
#include <esp_task_wdt.h>
#include "esp_system.h"

// 2. YOUR CUSTOM HEADERS
#include "Doser.h"
#include "AI_EngineV2.h"
#include "Allocator.h"
#include "ChemicalPresets.h"
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
#include "ApexLogEmulator.h"
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>

#define FIREBASE_API_KEY "AIzaSyB4XtC5Pvxw6To58EKTLMADQLqR_hTZK0M"
#define FIREBASE_DB_URL "https://aiesdoser-default-rtdb.firebaseio.com"

#include "../generated/device_config.generated.h"

// Permanent per-device Firebase Authentication credentials.
// These are generated automatically by scripts/build_device.py.
// There is intentionally no anonymous-authentication fallback.
#ifndef BUILD_FIREBASE_EMAIL
#error "BUILD_FIREBASE_EMAIL is missing from generated/device_config.generated.h"
#endif

#ifndef BUILD_FIREBASE_PASSWORD
#error "BUILD_FIREBASE_PASSWORD is missing from generated/device_config.generated.h"
#endif

// Build script writes BUILD_FW_VERSION into generated/device_config.generated.h.
// Keep the old FW_VERSION name so the rest of this file stays unchanged.
#define FW_VERSION BUILD_FW_VERSION

#include "Dashboard.h"

// Web route handlers used to be defined inline in this file. They now live
// in lib/WebRoutes/WebRoutes.cpp. WebRoutesShared.h provides the struct
// definitions (LightConfig, AlkDemandStore, etc.) that both this file and
// WebRoutes.cpp need to see.
#include "WebRoutesShared.h"
#include "WebRoutes.h"
#include "DemandLearning.h"

// --- DEFINITIONS (Allocating actual memory for the Truth) ---
bool emergencyStop = false;

// Local operating mode for the standalone page: 0=OFF, 1=AUTO, 2=MAN
int systemMode = 1;

// --- Version 2: Smoothing & Lighting Config ---
// struct LightConfig is now defined in lib/WebRoutes/WebRoutesShared.h
// (moved there so WebRoutes.cpp can see the type too). This is still the
// one real instance.
LightConfig lightConfig;

// Historical Smoothing Buffer
float alkHistory[5] = {0, 0, 0, 0, 0};
int alkIdx = 0;
// §8.5 chemistry targets. These were hardcoded compile-time constants with
// no customer-facing way to change them at all -- fixed 2026-07-25: now
// loaded from Preferences via loadChemistryTargets() (see near
// loadManualTestLocally()), these values are just the fallback defaults for
// a device that's never had targets explicitly set.
float targetAlk = 8.5f;
float targetCa  = 450.0f;
float targetMg  = 1440.0f;
// Added 2026-08-05: targetPhLow/targetPhHigh never existed anywhere in this
// file, on either the load, save, or AI-wiring side. Confirmed root cause
// of pH correction never actually working, even after the separate
// ingestMeasurement(P_PH,...) fix: with no target ever pushed into
// ai.targetPhLow/targetPhHigh, both stayed at their Recommendable default
// of 0.0 -- meaning any real pH reading always computed as "already above
// target" and got clamped to zero by the existing additive-only rule,
// regardless of whether pH was actually 8.6 or 7.4. 8.0/8.4 matches this
// codebase's own existing pH-ceiling default (mode7NaohMaxPh=8.45) and the
// generally accepted reef-tank healthy pH range.
float targetPhLow  = 8.0f;
float targetPhHigh = 8.4f;

// AI starts from this known daily demand, then adjusts up/down from water tests.
float baselineKalkMlDay  = 0.0f;
float baselineCacl2MlDay = 0.0f;
float baselineNaohMlDay  = 0.0f;
float baselineMgMlDay    = 0.0f;
String baselineCoralLoad = "custom";

// Rolling 7-day demand learning.
// false = collect/calculate/recommend only.
// true  = apply a bounded P4 Alk baseline adjustment after each valid rolling 7-day calculation.
bool automaticDemandLearningEnabled = true;
bool automaticCalciumLearningEnabled = true;

// REMOVED (v1 -> v2 migration): the old two-speed fastAlk boost/step-size
// learner (fastAlkBoostDkhDay / fastAlkLowSamples / fastAlkStableSamples and
// its ai.setFastAlkLearnerState/getFastAlkBoostDkhDay/etc. calls) is gone.
// v2's per-parameter Kalman filter maturity (ParamKalmanState::maturity(),
// MathEngine::maxCorrectionStep) provides the same "go faster early, taper
// as the estimate matures" behavior for all 4 water parameters, not just
// Alk, and needs no separate persisted counter set (see MIGRATION_NOTES.md,
// row 4). Persisted "alkfast_*" Preferences keys are simply no longer read.

// Optional one-time historical-demand bootstrap supplied by the per-device build.
// Every compatible device still receives normal rolling 7-day learning.
// These defaults keep older/generated headers compatible.
#ifndef BUILD_ALK_DEMAND_BOOTSTRAP_ENABLED
#define BUILD_ALK_DEMAND_BOOTSTRAP_ENABLED 0
#endif

#ifndef BUILD_ALK_DEMAND_BOOTSTRAP_DKH_DAY
#define BUILD_ALK_DEMAND_BOOTSTRAP_DKH_DAY 0.0f
#endif

// Customer-facing chemical recipe values.
// Dashboard users enter grams per gallon; dashboard/firmware still save the
// internal AI strength values used by the existing AI engine.
float recipeKalkGpg  = 12.0f;    // saturated kalk baseline: about 2 tsp/gal
float recipeAfrGpg   = 0.0f;     // commercial liquids normally use label/custom strength
float recipeAlkGpg   = 100.0f;   // soda ash baseline
float recipeNaohGpg  = 144.0f;   // Mark-style NaOH baseline
float recipeMgGpg    = 500.0f;   // magnesium baseline
float recipeCacl2Gpg = 250.0f;   // calcium chloride baseline
String recipeAfrType   = "tm_afr_powder";
String recipeAlkType   = "soda_ash";
String recipeMgType    = "mag_chloride";
String recipeCacl2Type = "cacl2_dihydrate";


// 1..8 dosing implementation used for pump mapping
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

// Apex is polled more often than Trident produces a new chemistry test. These
// fields distinguish a new Alk/Ca/Mg result from a repeated poll of the same
// result. pH is intentionally excluded because it changes continuously and is
// not proof that Trident completed a new test.
bool haveAcceptedTridentFingerprint = false;
float lastAcceptedTridentAlk = NAN;
float lastAcceptedTridentCa = NAN;
float lastAcceptedTridentMg = NAN;
bool newChemistrySamplePendingForDailyStats = false;
String lastAcceptedEmulatorTestTime = "";


// ---------------- LOW-COST FIREBASE ALERTS (reefDoser3 test) ----------------
// Dashboard setting:
//   muted   = no pushes
//   severe  = severe only
//   warning = severe + warning
//   info    = severe + warning + info
String notificationLevel = "warning";

// One compact node: /devices/<deviceId>/alertState
// Firmware only writes when alert state changes or a cooldown expires.
String currentAlertLevel = "normal";
String currentAlertCode = "OK";
String currentAlertMessage = "All monitored values normal";
bool currentAlertActive = false;
String emergencyStopReason = "";
unsigned long lastAlertWriteMs = 0;
unsigned long lastAlertEvalMs = 0;

const unsigned long ALERT_EVAL_EVERY_MS = 60000UL;                  // local check every 60s
const unsigned long ALERT_SEVERE_REPEAT_MS = 30UL * 60UL * 1000UL;  // max repeat every 30m
const unsigned long ALERT_WARNING_REPEAT_MS = 4UL * 60UL * 60UL * 1000UL; // max repeat every 4h
const unsigned long ALERT_INFO_REPEAT_MS = 24UL * 60UL * 60UL * 1000UL;   // max repeat every 24h

float DOSING_THRESHOLD = 1.0f; // Backward-compatible global default only
float pumpBuckets[4] = {0.0f, 0.0f, 0.0f, 0.0f};
float maxDoseLimit = 15.0f;    // Backward-compatible global max only

// ---------------- PER-PUMP DOSING SAFETIES ----------------
// Stored in Preferences/NVS under doser-settings. Physical pump order:
//   P1, P2, P3, P4. Mode 7 Eric map: P1=Kalk, P2=CaCl2, P3=NaOH, P4=Alk. Mode 8: P1=Kalk, P2=CaCl2, P3=NaOH, P4 unused.
// Threshold = bucket mL required before that pump is allowed to run.
// Max single dose = most mL allowed in one automatic/live dose command.
// Max daily dose = calendar-day limit for each physical pump.
float pumpDoseThresholdMl[4] = {100.0f, 10.0f, 5.0f, 5.0f};
float pumpMaxDoseMl[4]      = {1500.0f, 250.0f, 100.0f, 100.0f};
float pumpMaxDayMl[4]       = {35000.0f, 2000.0f, 1200.0f, 2500.0f};


// ---------------- MODE 7 DAY/NIGHT ALK SOURCE SPLIT ----------------
// Mode 7 physical map: P3 = NaOH, P4 = Alk solution. Mode 8 does not use P4 Alk.
// These settings control how Mode 7 splits alkalinity correction by light state.
// Defaults match Eric's test request: daytime=P4 Alk, nighttime=P3 NaOH.
bool mode7DayNightSplitEnabled = true;
float mode7DayNaohPct = 0.0f;
float mode7DayAlkPct = 100.0f;
float mode7NightNaohPct = 100.0f;
float mode7NightAlkPct = 0.0f;
float mode7NaohMaxPh = 8.45f;

// ---------------- AI CHEMISTRY SAFETIES ----------------
// These limit what the AI is allowed to request before the per-pump execution
// rails run. They are stored in Preferences/NVS under doser-settings.
float aiMaxKalkDayMl = 35000.0f;
float aiMaxNaohDayMl = 1200.0f;
float aiMaxAlkDayMl = 2500.0f;
float aiMaxAlkRiseDkhDay = 2.0f;
float aiMaxCaRisePpmDay = 20.0f;
float aiMaxMgCorrectionDayMl = 250.0f;
float aiMaxMgDayMl = 250.0f;
float aiMgDeadbandPpm = 25.0f;
extern float dailyDoseTotals[4]; // Links to Doser.cpp
// ---------------- LOCAL CHEMICAL RESERVOIR TRACKING ----------------
// Stored only in ESP32 Preferences/NVS. Volumes are NOT mirrored to Firebase.
// Dashboard can set bucket size in gallons; firmware subtracts actual dispensed mL.
extern const float ML_PER_GALLON = 3785.41f;
float chemicalCapacityGal[4] = {0.0f, 0.0f, 0.0f, 0.0f};
float chemicalRemainingMl[4] = {0.0f, 0.0f, 0.0f, 0.0f};

String lastApexDate = "";
String apexIp = "";

float pumpFlowRates[4] = {675.0f, 645.0f, 50.0f, 50.0f};
ManualTest lastLocalTest;
bool hasSavedManualTest = false;
// §8/§8.5 one-time setup wizard, lives on the local dashboard (not the
// cloud device-setup.html page, which only handles Wi-Fi + online-check
// before handing off here). Persisted so it doesn't show again once done.
bool setupWizardCompleted = false;

ApexApi apex;

// ============================================================================
// TEST ONLY: reefDoser3 Apex Log Emulator
// ----------------------------------------------------------------------------
// This does NOT change OTA and does NOT affect reefDoser1 or reefDoser2.
// When enabled, syncAllTruths() reads Eric's latest reefDoser2 Google Drive
// serial log through Apps Script instead of calling the real Apex.
//
// Controlled per-device via devices.json's "apexEmulatorTestEnabled" field
// (see scripts/build_device.py) rather than a hardcoded device ID here, so
// enabling this for a different bench/test unit is a devices.json change +
// rebuild for that one device, not a main.cpp edit shared by every device.
//
// Deploy AppsScript_ReefDoserLogApi.gs as a web app, then paste the /exec URL
// below. Use folder=roofDoser2 if the Drive folder is really misspelled that way.
// ============================================================================
#ifndef BUILD_APEX_EMULATOR_TEST_ENABLED
#define BUILD_APEX_EMULATOR_TEST_ENABLED 0
#endif
const char* APEX_EMULATOR_URL = "https://script.google.com/macros/s/AKfycbxN_NPXAxSR54WUfZmQeqPMv-S3GJzsQvhGHHWP2udwtONEoDrXordu-h_7tGUiXSPlwQ/exec?raw=1";
const unsigned long APEX_EMULATOR_POLL_MS = 1800000UL;

ApexLogEmulator apexEmu;

// Apex emulator HTTPS runs in its own task. The task never touches dosing,
// Firebase, LittleFS, or the AI engine. It only publishes a completed result
// through this small protected handoff for loopTask to consume safely.
TaskHandle_t apexEmulatorTaskHandle = nullptr;
portMUX_TYPE apexEmulatorResultMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool apexEmulatorResultPending = false;
volatile bool apexEmulatorResultOk = false;
volatile bool apexTlsReservation = false;
volatile unsigned long apexTlsCooldownUntilMs = 0;

bool apexTlsReservedOrCoolingDown() {
    if (apexTlsReservation) return true;
    return (long)(millis() - apexTlsCooldownUntilMs) < 0;
}

float apexEmulatorResultAlk = 0.0f;
float apexEmulatorResultCa = 0.0f;
float apexEmulatorResultMg = 0.0f;
float apexEmulatorResultPh = 0.0f;
char apexEmulatorResultSource[96] = {0};
char apexEmulatorResultLogTime[40] = {0};
char apexEmulatorResultError[160] = {0};

void apexEmulatorTask(void* parameter);
bool serviceApexEmulatorResult();

// Added 2026-08-05: isolated internet-reachability check, built specifically
// to fix the confirmed no-internet crash loop in connectToFirebase() without
// repeating either of two approaches that were tried and failed:
//   1. esp_task_wdt_reset() calls placed around the blocking Firebase calls
//      -- doesn't help, because DNS resolution (hostByName()) itself hangs
//      inside a SINGLE call for longer than the 30s watchdog window; a
//      reset placed between calls never gets a chance to run while one
//      call is still stuck.
//   2. config.timeout.* fields on FirebaseConfig -- doesn't help either,
//      because those only govern the socket/SSL/response stages AFTER DNS
//      resolves. DNS itself is handled by the underlying lwIP stack before
//      the Firebase library or its timeout config ever gets involved.
//
// This task does the same WiFi.hostByName() DNS lookup that was hanging,
// but on its OWN FreeRTOS task -- never registered with esp_task_wdt_add()
// (only the task that calls that in setup(), i.e. loopTask, is registered)
// -- so it is free to block through however long DNS takes without ever
// tripping the watchdog. It never touches writeFbdo, streamFbdo, config,
// auth, or anything else loopTask/OTA/emergency-stop use, so it carries
// none of the concurrency risk a full Firebase-call migration would.
// loop() only calls connectToFirebase() once this flag is true -- the
// risky call itself is completely unchanged and still runs on loopTask,
// synchronously, exactly as before. This does not make connectToFirebase()
// itself safe in isolation; it prevents it from ever running until a
// working path to the internet has already been confirmed.
volatile bool internetReachable = false;

void internetCheckTask(void* parameter) {
    (void)parameter;
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress resolvedIp;
            // Same hostname connectToFirebase()'s auth call ultimately
            // depends on. A successful resolve here is a good proxy for
            // "the network path Firebase needs is actually up," not just
            // "WiFi is associated to the router."
            bool ok = WiFi.hostByName("www.googleapis.com", resolvedIp);
            internetReachable = ok;
        } else {
            internetReachable = false;
        }

        // Recheck faster while down (so recovery is noticed promptly),
        // slower once confirmed up (no need to hammer DNS every cycle).
        // Fixed 2026-08-05: was 15000UL (15s) while offline. Suspected root
        // cause of dashboard requests taking up to ~2 minutes to complete
        // even after the separate client-side pileup fix -- ESP32's LWIP
        // network stack does not fully parallelize operations across
        // FreeRTOS tasks the way "this task is isolated" suggested it
        // would; a DNS lookup retried this often, each attempt taking
        // ~20s per the observed logs, can still contend with the web
        // server's own ability to accept/answer HTTP requests on a
        // different task. 3 minutes is a large enough gap to stop that
        // near-constant contention while offline, while still recovering
        // reasonably quickly once internet actually returns.
        vTaskDelay(pdMS_TO_TICKS(internetReachable ? 60000UL : 180000UL));
    }
}

TaskHandle_t internetCheckTaskHandle = nullptr;


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

// OTA safety gate. Once an OTA starts, all pumps are forced off and no new
// automatic dosing is allowed to start until the firmware reboots.
bool otaInProgress = false;
unsigned long otaStartedMs = 0;

// Firebase stream callbacks must not perform OTA network work directly.
// They only queue the request; loop() executes the OTA outside the callback.
bool pendingOtaRequested = false;
String pendingOtaUrl = "";
String pendingOtaSource = "";
bool emergencyStopBeforeOta = false;

// Push Apex/state mirrors to Firebase on a timer so RTDB is not hammered.
// Local dashboard still updates immediately from currentTemp/currentPh/currentAlk/etc.
const unsigned long FIREBASE_MIRROR_INTERVAL_MS = 300000UL; // 5 minutes
unsigned long lastFirebaseMirrorMs = 0;

// Low-cost AI plan publishing: write dosingMlPerDay only when the plan changes
// by more than 10%, or once per day as a refresh.
float lastPublishedPlanMlDay[6] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
// Fixed 2026-08-02: parallel tracking array for the new pump-indexed
// publish path (see publishAiPlanIfNeeded()) -- kept separate from
// lastPublishedPlanMlDay above rather than repurposing it, since the two
// arrays are indexed completely differently (legacy chemical-type slot vs.
// physical pump number) and conflating them would silently break the
// "did this actually change" comparison for both paths.
float lastPublishedPumpMlDay[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
String lastPlanPublishDate = "";
const float PLAN_PUBLISH_CHANGE_FRACTION = 0.10f;

// Bucket accumulation is tied to the dosing slice interval, not the 1-hour AI timer.
// This lets fresh Apex/manual AI plans fill buckets promptly while avoiding double-fills.
const unsigned long AI_BUCKET_INTERVAL_MS = 600000UL; // 10 minutes = 144 slots/day
unsigned long lastAiBucketAddMs = 0;

// Production safety: after OTA/reboot, do not auto-service restored dosing buckets
// immediately. This prevents a reboot in the middle of a slot from dumping the
// same restored bucket twice. Manual live-dose and calibration still work.
const unsigned long BOOT_DOSING_GRACE_MS = 120000UL; // 2 minutes


Provisioner provisioner;
//TODO new customer
String deviceID = BUILD_DEVICE_ID;

bool useApexLogEmulatorForThisDevice() {
    return BUILD_APEX_EMULATOR_TEST_ENABLED;
}

WebServer server(80);
AsyncWebServer serialServer(81);
Preferences prefs;

Doser doser;
// Global logger instance is defined in lib/Logger/logger.cpp via extern Logger logger;
CalibrationManager calManager(doser);
AIEngineV2 ai;
StateManager state;

// ============================================================================
// v1 -> v2 compatibility layer
// ----------------------------------------------------------------------------
// v1 exposed a fixed named-field plan (currentPlan.kalk/afr/alk/cacl2/naoh/mg).
// v2's DosingPlanV2 is indexed by declared-chemical slot instead, since the
// chemical list is now customer-declared (§5). This file (and WebRoutes.cpp)
// still has per-physical-pump wiring, safety logging, Firebase publishing,
// and dashboard JSON that all key off those six legacy chemical names, and
// none of that is AI_Engine's job to redesign. So: declare the six legacy
// chemicals into AIEngineV2 in a FIXED slot order every time the declaration
// needs rebuilding, and mirror the resulting DosingPlanV2 back into a
// same-shaped `currentPlan` global so every existing `currentPlan.X`
// call site (here and in WebRoutes.cpp) keeps compiling and behaving the
// same way. The LegacyChemSlot enum / LegacyPlanView struct themselves live
// in WebRoutesShared.h (not here) so both .cpp files see the identical
// type -- this is the one real `currentPlan` instance.
LegacyPlanView currentPlan;

// §5 free chemical declaration -- replaces the mode picker. See
// DASHBOARD_MIGRATION_PLAN.md for the full design. declaredChemicals[i]
// always corresponds index-for-index to ai.chemicals[i] (rebuilt fresh
// every cycle by rebuildAiChemicalDeclarations()) and to
// planMlPerDayByIndex[i] (populated by syncLegacyPlanFromV2()) -- the
// bucket scheduler relies on all three staying in lockstep by index.
DeclaredChemical declaredChemicals[kMaxDeclaredChemicals];
int declaredChemicalCount = 0;

// Last computed plan's mL/day, indexed to match declaredChemicals[] --
// the mode-agnostic replacement for reading currentPlan.kalk/afr/alk/
// cacl2/naoh/mg (which assumed exactly six fixed, named chemicals) in the
// AI bucket scheduler.
float planMlPerDayByIndex[kMaxDeclaredChemicals] = {0};

// Chemical strengths (dKH/mL for Alk-moving chemicals, ppm/mL for Ca/Mg-only
// chemicals). v1 kept these inside AIEngine and exposed them via
// ai.getDkhPerMlKalk()/etc getters; v2 has no such per-chemical accessor
// (the whole point of ChemicalDeclaration is that potency lives on the
// declaration, not on named engine methods), so this file now owns them
// directly as plain globals (also externed from WebRoutesShared.h so
// WebRoutes.cpp's chemical-strength dashboard routes see the same values).
//
// TODO(migration, safety-relevant): the fallback defaults below are NOT
// copied from the retired v1 AI_Engine.cpp, because that file was not part
// of the v2 delivery package (only AI_EngineV2/Allocator were). Every
// in-field device already has "str_kalk"/"str_afr"/etc. saved in the
// "doser-settings" Preferences namespace from before this migration, and
// "existing saved Preferences always win" (see loadChemicalStrengths()
// below), so these fallbacks only matter for a brand-new chip that has
// never saved a strength. CONFIRM these against the real removed v1
// defaults (or the dashboard's recipe-entry math) before flashing any
// factory-fresh unit.
float kalkStrengthDkhPerMl  = 0.014f;   // saturated kalkwasser, ~recipeKalkGpg-based
float afrStrengthDkhPerMl   = 0.0f;     // commercial AFR liquids vary by product/label
float alkStrengthDkhPerMl   = 0.056f;   // soda ash solution, ~recipeAlkGpg-based
float naohStrengthDkhPerMl  = 0.090f;   // NaOH solution, ~recipeNaohGpg-based
float mgStrengthPpmPerMl    = 2.20f;    // magnesium chloride solution
float cacl2StrengthPpmPerMl = 1.10f;    // calcium chloride solution

OtaManager ota;

float TANK_VOLUME_L = 1135.6f; // Default to 300 Gallons



float kalkBucket = 0.0f, alkBucket = 0.0f, caBucket = 0.0f, mgBucket = 0.0f;


unsigned long lastSliceMillis = 0;

const char* GOOGLE_LOG_URL ="https://script.google.com/macros/s/AKfycbzXjvrBBfkzdYW6BsBreZXV8lznugdBzwyS1jrxPXgs5zoFfi20-RVGqlsarmzbuUmsAw/exec";

// ---------------- GOOGLE DRIVE LOGGER DIAGNOSTICS ----------------
// These diagnostics only observe the local LittleFS log queue and network health.
// They do not change OTA, dosing, Firebase, or the logger upload behavior.
const unsigned long GDRIVE_DIAG_INTERVAL_MS = 300000UL; // 5 minutes
const unsigned long LOGGER_HEALTH_FIREBASE_INTERVAL_MS = 300000UL; // 5 minutes; tiny RTDB heartbeat for remote logger debugging
unsigned long lastGoogleDriveDiagMs = 0;
unsigned long lastLoggerHealthFirebaseMs = 0;
unsigned long googleDriveDiagCount = 0;

// struct GoogleDriveLogQueueStats is now defined in
// lib/WebRoutes/WebRoutesShared.h (moved there so WebRoutes.cpp can see
// the type too).

// Calibration timed runs are intentionally time-based, not mL-based.
// This prevents a bad/old calibration value from making a 60-second calibration run
// stop after only a few seconds.
bool calibrationRunActive[4] = {false, false, false, false};
unsigned long calibrationRunUntilMs[4] = {0, 0, 0, 0};

// ============================================================================
// HARD PUMP FAIL-SAFE SUPERVISOR
// ----------------------------------------------------------------------------
// Pumps are active HIGH on the current AIDoser hardware.
// These direct GPIO writes intentionally do not depend on Doser, LittleFS,
// Preferences, Wi-Fi, Firebase, or the logger.
//
// IMPORTANT: Software cannot turn a pump off while the CPU is physically frozen.
// Add 10 kOhm pull-down resistors to each pump input and use an external
// heartbeat-controlled pump-power enable for true hardware fail-safe protection.
// ============================================================================
static constexpr uint8_t SAFETY_PUMP_PINS[4] = {22, 25, 26, 27};
static constexpr uint32_t PUMP_RUNTIME_MARGIN_MS = 20000UL;
static constexpr uint32_t PUMP_RUNTIME_ABSOLUTE_MAX_MS = 5UL * 60UL * 1000UL;
static constexpr uint32_t INTER_PUMP_DELAY_MS = 2UL * 60UL * 1000UL;

int safetyActivePump = -1;
unsigned long safetyPumpDeadlineMs = 0;
bool pumpRuntimeSafetyTripped = false;

// Chemical-separation lockout. After an automatic or Quick Dose pump finishes,
// no other dosing pump may start for five full minutes. Calibration runs are
// intentionally excluded so the calibration workflow remains usable.
bool interPumpDoseActive = false;
bool interPumpDelayActive = false;
unsigned long lastPumpDoseFinishedMs = 0;
int interPumpLastPump = -1;
String interPumpLastSource = "";

void forcePumpPinsOffDirect() {
    for (int i = 0; i < 4; ++i) {
        pinMode(SAFETY_PUMP_PINS[i], OUTPUT);
        digitalWrite(SAFETY_PUMP_PINS[i], LOW);
    }
}

bool anyDoserPumpRunning() {
    for (int i = 0; i < 4; ++i) {
        if (doser.isPumpRunning(i)) return true;
    }
    return false;
}

void noteInterPumpDoseStarted(int pumpIndex, const char* source) {
    interPumpDoseActive = true;
    interPumpLastPump = pumpIndex;
    interPumpLastSource = source ? source : "Dose";
}

void serviceInterPumpDelay() {
    // A dose that we started has completed as soon as no doser pump is running.
    // Start the separation period from that actual completion time.
    if (interPumpDoseActive && !anyDoserPumpRunning()) {
        interPumpDoseActive = false;
        interPumpDelayActive = true;
        lastPumpDoseFinishedMs = millis();

        // Fixed 2026-08-04: "300 seconds" was a hardcoded literal, not
        // calculated from INTER_PUMP_DELAY_MS -- confirmed misleading when
        // the constant was changed to 2 minutes and this text kept saying
        // 300 regardless. Now reports whatever the constant actually is.
        const unsigned long delaySeconds = INTER_PUMP_DELAY_MS / 1000UL;
        Serial.printf(
            "INTER-PUMP DELAY STARTED [%s]: P%d finished; next dose allowed in %lu seconds.\n",
            interPumpLastSource.c_str(),
            interPumpLastPump + 1,
            delaySeconds
        );
        logger.printf(
            "INTER-PUMP DELAY STARTED [%s]: P%d finished; next dose allowed in %lu seconds.\n",
            interPumpLastSource.c_str(),
            interPumpLastPump + 1,
            delaySeconds
        );
    }

    if (interPumpDelayActive &&
        (unsigned long)(millis() - lastPumpDoseFinishedMs) >= INTER_PUMP_DELAY_MS) {
        interPumpDelayActive = false;
        Serial.println("INTER-PUMP DELAY COMPLETE: next pump dose may start.");
        logger.println("INTER-PUMP DELAY COMPLETE: next pump dose may start.");
    }
}

bool interPumpDelayReady(unsigned long* remainingMs = nullptr) {
    if (!interPumpDelayActive) {
        if (remainingMs) *remainingMs = 0;
        return true;
    }

    const unsigned long elapsed = millis() - lastPumpDoseFinishedMs;
    if (elapsed >= INTER_PUMP_DELAY_MS) {
        interPumpDelayActive = false;
        if (remainingMs) *remainingMs = 0;
        return true;
    }

    if (remainingMs) *remainingMs = INTER_PUMP_DELAY_MS - elapsed;
    return false;
}

void clearPumpRuntimeDeadline() {
    safetyActivePump = -1;
    safetyPumpDeadlineMs = 0;
}

void armPumpRuntimeDeadlineMs(int pumpIndex, unsigned long requestedRunMs, const char* source) {
    if (pumpIndex < 0 || pumpIndex > 3) return;

    unsigned long safeRunMs = requestedRunMs + PUMP_RUNTIME_MARGIN_MS;
    if (safeRunMs < 1000UL) safeRunMs = 1000UL;
    if (safeRunMs > PUMP_RUNTIME_ABSOLUTE_MAX_MS) {
        safeRunMs = PUMP_RUNTIME_ABSOLUTE_MAX_MS;
    }

    safetyActivePump = pumpIndex;
    safetyPumpDeadlineMs = millis() + safeRunMs;
    pumpRuntimeSafetyTripped = false;

    Serial.printf(
        "PUMP RUNTIME SAFETY ARMED [%s]: P%d deadline in %lu ms\n",
        source ? source : "dose",
        pumpIndex + 1,
        safeRunMs
    );
}

void armPumpRuntimeDeadlineForMl(int pumpIndex, float ml, const char* source) {
    float flow = (pumpIndex >= 0 && pumpIndex < 4) ? pumpFlowRates[pumpIndex] : 0.0f;

    // Invalid flow must never create an unlimited run. Use the absolute cap.
    unsigned long expectedMs = PUMP_RUNTIME_ABSOLUTE_MAX_MS - PUMP_RUNTIME_MARGIN_MS;

    if (isfinite(flow) && flow > 0.01f && isfinite(ml) && ml > 0.0f) {
        double calculatedMs = (static_cast<double>(ml) / static_cast<double>(flow)) * 60000.0;
        if (calculatedMs < 1.0) calculatedMs = 1.0;
        if (calculatedMs > static_cast<double>(PUMP_RUNTIME_ABSOLUTE_MAX_MS - PUMP_RUNTIME_MARGIN_MS)) {
            calculatedMs = static_cast<double>(PUMP_RUNTIME_ABSOLUTE_MAX_MS - PUMP_RUNTIME_MARGIN_MS);
        }
        expectedMs = static_cast<unsigned long>(calculatedMs);
    }

    armPumpRuntimeDeadlineMs(pumpIndex, expectedMs, source);
}

// Centralized emergency-stop path. Every automatic or operator-triggered stop
// should go through this function so the persistent log records the first cause.
bool publishAlertState(const String& level, const String& code, const String& message, bool active, const char* source, bool force);

void triggerEmergencyStop(const String& reason, const char* source = "Safety") {
    const bool wasAlreadyActive = emergencyStop;

    doser.stopAllPumps();
    forcePumpPinsOffDirect();
    clearPumpRuntimeDeadline();
    emergencyStop = true;
    emergencyStopReason = reason;

    if (!wasAlreadyActive) {
        Serial.printf("EMERGENCY STOP TRIGGERED [%s]: %s\n", source ? source : "Safety", reason.c_str());
        logger.printf("EMERGENCY STOP TRIGGERED [%s]: %s\n", source ? source : "Safety", reason.c_str());

        if (WiFi.status() == WL_CONNECTED) {
            Firebase.setBool(writeFbdo, ("/devices/" + deviceID + "/state/emergencyStop").c_str(), true);
            Firebase.setString(writeFbdo, ("/devices/" + deviceID + "/state/emergencyStopReason").c_str(), reason);
        }

        publishAlertState("severe", "EMERGENCY_STOP", reason, true, source ? source : "Safety", true);
    }
}

void servicePumpRuntimeSafety() {
    bool running = anyDoserPumpRunning();

    if (!running) {
        clearPumpRuntimeDeadline();
        return;
    }

    // A pump running without an armed deadline is itself unsafe.
    if (safetyPumpDeadlineMs == 0) {
        pumpRuntimeSafetyTripped = true;

        int runningPump = -1;
        for (int i = 0; i < 4; ++i) {
            if (doser.isPumpRunning(i)) {
                runningPump = i;
                break;
            }
        }

        String reason = "Pump running without an armed runtime deadline";
        if (runningPump >= 0) {
            reason += " (P" + String(runningPump + 1) +
                      ", bucket=" + String(pumpBuckets[runningPump], 2) + " mL)";
        }
        triggerEmergencyStop(reason, "PumpRuntime");
        return;
    }

    if ((long)(millis() - safetyPumpDeadlineMs) >= 0) {
        const int trippedPump = safetyActivePump;
        const unsigned long overdueMs = millis() - safetyPumpDeadlineMs;

        for (int i = 0; i < 4; ++i) {
            calibrationRunActive[i] = false;
            calibrationRunUntilMs[i] = 0;
        }

        pumpRuntimeSafetyTripped = true;

        String reason = "Pump exceeded its runtime deadline";
        if (trippedPump >= 0 && trippedPump < 4) {
            reason += " (P" + String(trippedPump + 1) +
                      ", overdue=" + String(overdueMs) + " ms" +
                      ", bucket=" + String(pumpBuckets[trippedPump], 2) + " mL)";
        }
        triggerEmergencyStop(reason, "PumpRuntime");
    }
}

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

// ---------------- ROLLING 7-DAY ALK DEMAND LEARNER ----------------
// One compact binary file is written once per completed day. Preferences only
// stores the resulting baseline because it is a setting, not time-series data.
// ALK_DEMAND_HISTORY_FILE now lives in lib/WebRoutes/WebRoutesShared.h.
static constexpr float ERIC_SEEDED_DAILY_DEMAND_DKH = 0.73f;

// struct AlkDemandDay/AlkDemandStore and the ALK_DEMAND_MAGIC/VERSION/
// MAX_DAYS constants are now defined in lib/WebRoutes/WebRoutesShared.h
// (moved there so WebRoutes.cpp can see the type too). This is still the
// one real instance.
AlkDemandStore alkDemandStore;

// ---------------- ROLLING 7-DAY CALCIUM DEMAND LEARNER ----------------
// Calcium added by kalk is derived from balanced calcification stoichiometry:
// approximately 7.143 ppm Ca accompanies each 1.0 dKH supplied by kalk.
// CA_DEMAND_HISTORY_FILE and CA_PPM_PER_DKH_KALK now live in
// lib/WebRoutes/WebRoutesShared.h.

// struct CalciumDemandDay/CalciumDemandStore and the CA_DEMAND_MAGIC/
// VERSION/MAX_DAYS constants are now defined in
// lib/WebRoutes/WebRoutesShared.h (moved there so WebRoutes.cpp can see
// the type too). This is still the one real instance.
CalciumDemandStore calciumDemandStore;


// loadAlkDemandLearningSetting() moved to lib/DemandLearning/DemandLearning.cpp

// saveAlkDemandLearningSetting() moved to lib/DemandLearning/DemandLearning.cpp

// handleGetAlkDemandLearning() moved to lib/WebRoutes/WebRoutes.cpp

bool publishAlkDemandStatusToFirebase(const char* source, bool force);
bool publishCalciumDemandStatusToFirebase(const char* source, bool force);
bool saveCalciumDemandHistory();
void loadCalciumDemandHistory();
void recordCompletedCalciumDayAndLearn(float avgCa);
void printCalciumDemandRecommendation(const char* source);


// loadCalciumDemandLearningSetting() moved to lib/DemandLearning/DemandLearning.cpp

// saveCalciumDemandLearningSetting() moved to lib/DemandLearning/DemandLearning.cpp

// handleGetCalciumDemandLearning() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostCalciumDemandLearning() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostAlkDemandLearning() moved to lib/WebRoutes/WebRoutes.cpp
bool saveAlkDemandHistory();

void loadAlkDemandHistory();
void recordCompletedDayAndLearn(float avgAlk);
void printAlkDemandRecommendation(const char* source);
void connectToFirebase();
void tokenStatusCallback(TokenInfo info);
void streamTimeoutCallback(bool timeout);
void streamCallback(StreamData data);
bool acceptNewChemistryMeasurement(const char* source, const String& measurementId, bool forceNew);
void syncAllTruths();
bool syncApexLogEmulatorTruths();
void apexEmulatorTask(void* parameter);
bool serviceApexEmulatorResult();

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
void handleGetAlkDemandLearning();
void handlePostAlkDemandLearning();
void loadAlkDemandLearningSetting();
void saveAlkDemandLearningSetting();
void loadCalciumDemandLearningSetting();
void saveCalciumDemandLearningSetting();
void handleGetCalciumDemandLearning();
void handlePostCalciumDemandLearning();
void handleGetChemicalLevels();
void handlePostChemicalLevels();
void loadChemicalStrengths();
void loadChemicalRecipes();
void saveChemicalRecipes();
void handlePostChemicalStrengths();
void loadMode7DayNightSplit();
void saveMode7DayNightSplit();
void applyMode7DayNightSplitToEngine();
void handlePostMode7DayNightSplit();
void loadAiChemistrySafeties();
void saveAiChemistrySafeties();
void applyAiChemistrySafetiesToEngine();
void handlePostAiChemistrySafeties();
void loadPumpSafeties();
void savePumpSafeties();
float getPumpDoseThresholdMl(int pumpIndex);
float getPumpMaxDoseMl(int pumpIndex);
float getPumpMaxDayMl(int pumpIndex);
float getPumpDailyRemainingMl(int pumpIndex);
float applyPumpSafetyCaps(int pumpIndex, float requestedMl, const char* source);

void saveManualTestLocally(float alk, float ca, float mg, float ph);
void loadManualTestLocally();
void runOneTimeKalkLimitMigration();
void loadLocalSettings();
void loadFlowRates();
void loadChemicalReservoirs();
void saveChemicalReservoirs();
void recordChemicalDispense(int pumpIndex, float ml, const char* source);
void saveFlowRate(int idx, float flowMlPerMin);
bool mirrorStatusToFirebase();
bool publishTankVolumeToFirebase(const char* source);
int getLocalHour();
void updateDailyAverages();
void pushDailyReport();
void loadDosingState();
void saveDosingState();
GoogleDriveLogQueueStats collectGoogleDriveLogQueueStats();
void logGoogleDriveDiagnostics(const char* source);
bool publishLoggerHealthToFirebase(const char* source, bool force = false);
void addCurrentAiPlanToBuckets(const char* source, bool force = false);
void publishAiPlanIfNeeded(const char* source, bool force = false);
bool isLightsOn();
void publishFirmwareVersionIfReady(bool force = false);
void prepareForOtaUpdate(const String& firmwareUrl);
void serviceOtaCommandFallback();
void evaluateAlertState(const char* source = "loop", bool force = false);
bool publishAlertState(const String& level, const String& code, const String& message, bool active, const char* source, bool force = false);
void handleGetNotificationSettings();
void handlePostNotificationSettings();
bool isValidNotificationLevel(const String& level);
void forcePumpPinsOffDirect();
bool anyDoserPumpRunning();
void clearPumpRuntimeDeadline();
void armPumpRuntimeDeadlineMs(int pumpIndex, unsigned long requestedRunMs, const char* source);
void armPumpRuntimeDeadlineForMl(int pumpIndex, float ml, const char* source);
void servicePumpRuntimeSafety();

GoogleDriveLogQueueStats collectGoogleDriveLogQueueStats() {
    GoogleDriveLogQueueStats stats;

    File root = LittleFS.open("/logs");
    if (!root || !root.isDirectory()) {
        return stats;
    }

    File file = root.openNextFile();
    while (file) {
        String path = file.path();
        size_t sz = file.size();

        // Count only logger files. This keeps unrelated LittleFS files out of the queue number.
        if (path.endsWith(".log")) {
            stats.fileCount++;
            stats.totalBytes += sz;

            // Logger filenames include timestamps/sequence numbers, so lexical order is useful enough
            // for debugging oldest/newest queued Google Drive uploads.
            if (stats.oldestPath == "none" || path < stats.oldestPath) {
                stats.oldestPath = path;
                stats.oldestSize = sz;
            }
            if (stats.newestPath == "none" || path > stats.newestPath) {
                stats.newestPath = path;
                stats.newestSize = sz;
            }
        }

        file.close();
        file = root.openNextFile();
    }

    root.close();
    return stats;
}

void logGoogleDriveDiagnostics(const char* source) {
    GoogleDriveLogQueueStats q = collectGoogleDriveLogQueueStats();
    googleDriveDiagCount++;

    String ip = WiFi.localIP().toString();
    String gw = WiFi.gatewayIP().toString();
    String dns = WiFi.dnsIP().toString();

    Serial.printf("[GDRIVE DIAG #%lu %s] up=%lus wifi=%d rssi=%d ip=%s gw=%s dns=%s heap=%u minHeap=%u fsUsed=%u fsTotal=%u queuedFiles=%d queuedBytes=%u oldest=%s(%u) newest=%s(%u)\n",
                  googleDriveDiagCount,
                  source ? source : "diag",
                  millis() / 1000UL,
                  WiFi.status(),
                  WiFi.RSSI(),
                  ip.c_str(),
                  gw.c_str(),
                  dns.c_str(),
                  ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(),
                  LittleFS.usedBytes(),
                  LittleFS.totalBytes(),
                  q.fileCount,
                  (unsigned)q.totalBytes,
                  q.oldestPath.c_str(),
                  (unsigned)q.oldestSize,
                  q.newestPath.c_str(),
                  (unsigned)q.newestSize);

    logger.printf("[GDRIVE DIAG #%lu %s] up=%lus wifi=%d rssi=%d ip=%s gw=%s dns=%s heap=%u minHeap=%u fsUsed=%u fsTotal=%u queuedFiles=%d queuedBytes=%u oldest=%s(%u) newest=%s(%u)\n",
                  googleDriveDiagCount,
                  source ? source : "diag",
                  millis() / 1000UL,
                  WiFi.status(),
                  WiFi.RSSI(),
                  ip.c_str(),
                  gw.c_str(),
                  dns.c_str(),
                  ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(),
                  LittleFS.usedBytes(),
                  LittleFS.totalBytes(),
                  q.fileCount,
                  (unsigned)q.totalBytes,
                  q.oldestPath.c_str(),
                  (unsigned)q.oldestSize,
                  q.newestPath.c_str(),
                  (unsigned)q.newestSize);
}


bool publishLoggerHealthToFirebase(const char* source, bool force) {
    if (apexTlsReservedOrCoolingDown()) return false;
    if (WiFi.status() != WL_CONNECTED || !firebaseStarted || !Firebase.ready()) {
        return false;
    }

    unsigned long nowMs = millis();
    if (!force && lastLoggerHealthFirebaseMs != 0 &&
        (nowMs - lastLoggerHealthFirebaseMs) < LOGGER_HEALTH_FIREBASE_INTERVAL_MS) {
        return false;
    }
    lastLoggerHealthFirebaseMs = nowMs;

    GoogleDriveLogQueueStats q = collectGoogleDriveLogQueueStats();

    FirebaseJson json;
    json.set("updatedAtSec", (uint32_t)(nowMs / 1000UL));
    json.set("source", source ? source : "loggerHealth");
    json.set("wifiStatus", (int)WiFi.status());
    json.set("rssi", WiFi.RSSI());
    json.set("ip", WiFi.localIP().toString());
    json.set("gateway", WiFi.gatewayIP().toString());
    json.set("dns", WiFi.dnsIP().toString());
    json.set("freeHeap", (uint32_t)ESP.getFreeHeap());
    json.set("minFreeHeap", (uint32_t)ESP.getMinFreeHeap());
    json.set("littleFsUsed", (uint32_t)LittleFS.usedBytes());
    json.set("littleFsTotal", (uint32_t)LittleFS.totalBytes());
    json.set("queuedFiles", q.fileCount);
    json.set("queuedBytes", (uint32_t)q.totalBytes);
    json.set("oldestFile", q.oldestPath);
    json.set("oldestBytes", (uint32_t)q.oldestSize);
    json.set("newestFile", q.newestPath);
    json.set("newestBytes", (uint32_t)q.newestSize);

    String path = "/devices/" + deviceID + "/loggerHealth";
    if (Firebase.updateNode(writeFbdo, path.c_str(), json)) {
        static unsigned long lastOkLogMs = 0;
        if (force || nowMs - lastOkLogMs > 600000UL) {
            lastOkLogMs = nowMs;
            Serial.printf("[LOGGER HEALTH FIREBASE OK] source=%s queuedFiles=%d queuedBytes=%u rssi=%d heap=%u\n",
                          source ? source : "loggerHealth",
                          q.fileCount,
                          (unsigned)q.totalBytes,
                          WiFi.RSSI(),
                          ESP.getFreeHeap());
            logger.printf("[LOGGER HEALTH FIREBASE OK] source=%s queuedFiles=%d queuedBytes=%u rssi=%d heap=%u\n",
                          source ? source : "loggerHealth",
                          q.fileCount,
                          (unsigned)q.totalBytes,
                          WiFi.RSSI(),
                          ESP.getFreeHeap());
        }
        return true;
    }

    Serial.printf("[LOGGER HEALTH FIREBASE FAILED] source=%s error=%s\n",
                  source ? source : "loggerHealth",
                  writeFbdo.errorReason().c_str());
    logger.printf("[LOGGER HEALTH FIREBASE FAILED] source=%s error=%s\n",
                  source ? source : "loggerHealth",
                  writeFbdo.errorReason().c_str());
    return false;
}

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
        case 7: // Mode 7: Kalk + CaCl2 + NaOH + Alk using old Mg pump
            switch (idx) {
                case 0: return "kalk";
                case 1: return "cacl2";
                case 2: return "naoh";
                case 3: return "alk";
                default: return "unused";
            }
        case 8: // Mode 8: Mode 7 with Alk pump removed: P1 Kalk, P2 CaCl2, P3 NaOH, P4 unused
            switch (idx) {
                case 0: return "kalk";
                case 1: return "cacl2";
                case 2: return "naoh";
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
        case 7:
            return 4;
        case 8:
            return 3;
        default:
            return 1;
    }
}

const char* pumpSafetyThresholdKey(int idx) {
    switch (idx) {
        case 0: return "saf_p1_thr";
        case 1: return "saf_p2_thr";
        case 2: return "saf_p3_thr";
        case 3: return "saf_p4_thr";
        default: return "saf_p1_thr";
    }
}

const char* pumpSafetyMaxDoseKey(int idx) {
    switch (idx) {
        case 0: return "saf_p1_max";
        case 1: return "saf_p2_max";
        case 2: return "saf_p3_max";
        case 3: return "saf_p4_max";
        default: return "saf_p1_max";
    }
}

const char* pumpSafetyMaxDayKey(int idx) {
    switch (idx) {
        case 0: return "saf_p1_day";
        case 1: return "saf_p2_day";
        case 2: return "saf_p3_day";
        case 3: return "saf_p4_day";
        default: return "saf_p1_day";
    }
}

float currentTankGallonsForDefaults() {
    float gal = TANK_VOLUME_L / 3.78541f;
    if (!isfinite(gal) || gal <= 0.0f) gal = 300.0f;
    return gal;
}

float defaultScale300To1100() {
    // Scale defaults between known working systems:
    //   reefDoser1 / Mark = 300 gallons
    //   reefDoser3 / Eric profile = 1100 gallons
    // Clamp outside that range so new small/huge tanks do not get absurd values.
    float gal = currentTankGallonsForDefaults();
    float scale = (gal - 300.0f) / 800.0f;
    if (scale < 0.0f) scale = 0.0f;
    if (scale > 1.0f) scale = 1.0f;
    return scale;
}

float scaledDefault300To1100(float mark300Value, float eric1100Value) {
    float s = defaultScale300To1100();
    return mark300Value + (eric1100Value - mark300Value) * s;
}

float defaultMode7KalkBaselineMlDay() {
    // Missing/zero Mode 7 baseline fallback. Existing positive saved values still win.
    // Known working references: Mark 300g ~= 2500 ml/day, Eric 1100g ~= 35000 ml/day.
    return scaledDefault300To1100(2500.0f, 35000.0f);
}


float defaultPumpThresholdMl(int idx) {
    // Missing Preference only. Existing saved values are never changed.
    // Physical pump order. Mode 7 Eric map: P1=Kalk, P2=CaCl2, P3=NaOH, P4=Alk. Mode 8: P1=Kalk, P2=CaCl2, P3=NaOH, P4 unused.
    switch (idx) {
        case 0: return scaledDefault300To1100(20.0f, 100.0f);  // Kalk needs larger accurate dumps on big tanks
        case 1: return scaledDefault300To1100(10.0f, 10.0f);   // CaCl2
        case 2: return scaledDefault300To1100(5.0f, 5.0f);     // NaOH
        case 3: return scaledDefault300To1100(5.0f, 5.0f);     // Alk solution
        default: return 1.0f;
    }
}

float defaultPumpMaxDoseMl(int idx) {
    // Max mL allowed in one dose/run when no saved Preference exists.
    switch (idx) {
        case 0: return scaledDefault300To1100(250.0f, 1500.0f);
        case 1: return scaledDefault300To1100(100.0f, 250.0f);
        case 2: return scaledDefault300To1100(50.0f, 100.0f);
        case 3: return scaledDefault300To1100(50.0f, 100.0f);
        default: return 15.0f;
    }
}

float defaultPumpMaxDayMl(int idx) {
    // Max mL/day per physical pump when no saved Preference exists.
    switch (idx) {
        case 0: return scaledDefault300To1100(2500.0f, 35000.0f);
        case 1: return scaledDefault300To1100(500.0f, 2000.0f);
        case 2: return scaledDefault300To1100(100.0f, 1200.0f);
        case 3: return scaledDefault300To1100(500.0f, 2500.0f);
        default: return 1000.0f;
    }
}

float sanitizeSafetyValue(float value, float fallback, float minVal, float maxVal) {
    if (!isfinite(value) || value < minVal) return fallback;
    if (value > maxVal) return maxVal;
    return value;
}

void loadPumpSafeties() {
    prefs.begin("doser-settings", true);

    // Old global keys are used only as fallback for older units.
    // IMPORTANT: only use them if the old keys actually exist. On a fresh chip,
    // prefs.getFloat("d_thresh", DOSING_THRESHOLD) returns the compiled global
    // default 1.0, which incorrectly overwrote the per-pump defaults.
    bool hasOldThreshold = prefs.isKey("d_thresh");
    bool hasOldMax = prefs.isKey("d_max");
    float oldThreshold = hasOldThreshold ? prefs.getFloat("d_thresh", DOSING_THRESHOLD) : NAN;
    float oldMax = hasOldMax ? prefs.getFloat("d_max", maxDoseLimit) : NAN;

    for (int i = 0; i < 4; ++i) {
        float thrFallback = defaultPumpThresholdMl(i);
        float maxFallback = defaultPumpMaxDoseMl(i);
        float dayFallback = defaultPumpMaxDayMl(i);

        if (prefs.isKey(pumpSafetyThresholdKey(i))) {
            pumpDoseThresholdMl[i] = sanitizeSafetyValue(prefs.getFloat(pumpSafetyThresholdKey(i), thrFallback), thrFallback, 0.1f, 10000.0f);
        } else if (hasOldThreshold && isfinite(oldThreshold) && oldThreshold > 0.0f) {
            // Preserve old dashboard setting on first update when per-pump keys do not exist yet.
            pumpDoseThresholdMl[i] = sanitizeSafetyValue(oldThreshold, thrFallback, 0.1f, 10000.0f);
        } else {
            pumpDoseThresholdMl[i] = thrFallback;
        }

        if (prefs.isKey(pumpSafetyMaxDoseKey(i))) {
            pumpMaxDoseMl[i] = sanitizeSafetyValue(prefs.getFloat(pumpSafetyMaxDoseKey(i), maxFallback), maxFallback, 1.0f, 100000.0f);
        } else if (hasOldMax && isfinite(oldMax) && oldMax > 0.0f) {
            // Preserve old dashboard setting on first update when per-pump keys do not exist yet.
            pumpMaxDoseMl[i] = sanitizeSafetyValue(oldMax, maxFallback, 1.0f, 100000.0f);
        } else {
            pumpMaxDoseMl[i] = maxFallback;
        }

        pumpMaxDayMl[i] = sanitizeSafetyValue(prefs.getFloat(pumpSafetyMaxDayKey(i), dayFallback), dayFallback, 1.0f, 250000.0f);
    }
    prefs.end();

    // Backward-compatible aliases for old dashboard/status fields.
    DOSING_THRESHOLD = pumpDoseThresholdMl[0];
    maxDoseLimit = pumpMaxDoseMl[0];
}

void savePumpSafeties() {
    prefs.begin("doser-settings", false);
    for (int i = 0; i < 4; ++i) {
        prefs.putFloat(pumpSafetyThresholdKey(i), pumpDoseThresholdMl[i]);
        prefs.putFloat(pumpSafetyMaxDoseKey(i), pumpMaxDoseMl[i]);
        prefs.putFloat(pumpSafetyMaxDayKey(i), pumpMaxDayMl[i]);
    }

    // Keep old keys populated for backward compatibility with older dashboard builds.
    prefs.putFloat("d_thresh", pumpDoseThresholdMl[0]);
    prefs.putFloat("d_max", pumpMaxDoseMl[0]);
    prefs.end();
}

// Added 2026-08-04: see DeclaredChemical::bucketThresholdMl's comment for
// the full incident. Single source of truth for "which chemical, if any,
// is on this physical pump right now" -- used by the three getters below
// so threshold/max-single-dose/max-day all resolve the same way.
int chemicalIndexForPump(int pumpIndex) {
    if (pumpIndex < 0 || pumpIndex >= 4) return -1;
    for (int i = 0; i < declaredChemicalCount; i++) {
        if (declaredChemicals[i].active && declaredChemicals[i].pumpIndex == pumpIndex) return i;
    }
    return -1;
}

float getPumpDoseThresholdMl(int pumpIndex) {
    int ci = chemicalIndexForPump(pumpIndex);
    if (ci >= 0) {
        float v = declaredChemicals[ci].bucketThresholdMl;
        if (isfinite(v) && v > 0.0f) return v; // chemical-level value takes priority
    }
    if (pumpIndex >= 0 && pumpIndex < 4) {
        float v = pumpDoseThresholdMl[pumpIndex];
        if (isfinite(v) && v > 0.0f) return v; // fallback: not yet configured per-chemical
    }
    return DOSING_THRESHOLD > 0.0f ? DOSING_THRESHOLD : 1.0f;
}

float getPumpMaxDoseMl(int pumpIndex) {
    int ci = chemicalIndexForPump(pumpIndex);
    if (ci >= 0) {
        float v = declaredChemicals[ci].maxSingleDoseMl;
        if (isfinite(v) && v > 0.0f) return v;
    }
    if (pumpIndex >= 0 && pumpIndex < 4) {
        float v = pumpMaxDoseMl[pumpIndex];
        if (isfinite(v) && v > 0.0f) return v;
    }
    return maxDoseLimit > 0.0f ? maxDoseLimit : 15.0f;
}

float getPumpMaxDayMl(int pumpIndex) {
    // Fixed 2026-08-04: this is the actual PHYSICAL EXECUTION enforcement
    // (see applyPumpSafetyCaps() below) -- previously read ONLY the
    // pump-indexed array, completely independent of this same chemical's
    // maxMlPerDay that the allocator plans against (Manage Chemicals'
    // "Daily Dose Cap"). Now chemical-level takes priority, same pattern
    // as the two getters above, so planning and physical dispensing can
    // no longer silently disagree about the same chemical's daily cap.
    int ci = chemicalIndexForPump(pumpIndex);
    if (ci >= 0) {
        float v = declaredChemicals[ci].maxMlPerDay;
        if (isfinite(v) && v > 0.0f) return v;
    }
    if (pumpIndex >= 0 && pumpIndex < 4) {
        float v = pumpMaxDayMl[pumpIndex];
        if (isfinite(v) && v > 0.0f) return v;
    }
    return 1000.0f;
}

float getPumpDailyRemainingMl(int pumpIndex) {
    if (pumpIndex < 0 || pumpIndex >= 4) return 0.0f;
    float used = dailyDoseTotals[pumpIndex];
    if (!isfinite(used) || used < 0.0f) used = 0.0f;
    float remaining = getPumpMaxDayMl(pumpIndex) - used;
    return remaining > 0.0f ? remaining : 0.0f;
}

float applyPumpSafetyCaps(int pumpIndex, float requestedMl, const char* source) {
    if (pumpIndex < 0 || pumpIndex >= 4 || requestedMl <= 0.0f) return 0.0f;

    float maxDose = getPumpMaxDoseMl(pumpIndex);
    float dailyRemaining = getPumpDailyRemainingMl(pumpIndex);
    float capped = requestedMl;

    if (capped > maxDose) capped = maxDose;
    if (capped > dailyRemaining) capped = dailyRemaining;

    if (capped < 0.01f) {
        Serial.printf("PUMP SAFETY [%s]: P%d blocked. requested=%.2f maxDose=%.2f dailyUsed=%.2f maxDay=%.2f\n",
                      source ? source : "dose", pumpIndex + 1, requestedMl, maxDose,
                      dailyDoseTotals[pumpIndex], getPumpMaxDayMl(pumpIndex));
        logger.printf("PUMP SAFETY [%s]: P%d blocked. requested=%.2f maxDose=%.2f dailyUsed=%.2f maxDay=%.2f\n",
                      source ? source : "dose", pumpIndex + 1, requestedMl, maxDose,
                      dailyDoseTotals[pumpIndex], getPumpMaxDayMl(pumpIndex));
        return 0.0f;
    }

    if (capped < requestedMl - 0.01f) {
        Serial.printf("PUMP SAFETY [%s]: P%d capped %.2f ml -> %.2f ml. maxDose=%.2f dailyRemaining=%.2f\n",
                      source ? source : "dose", pumpIndex + 1, requestedMl, capped, maxDose, dailyRemaining);
        logger.printf("PUMP SAFETY [%s]: P%d capped %.2f ml -> %.2f ml. maxDose=%.2f dailyRemaining=%.2f\n",
                      source ? source : "dose", pumpIndex + 1, requestedMl, capped, maxDose, dailyRemaining);
    }

    return capped;
}


float sanitizePercent(float value, float fallback) {
    if (!isfinite(value)) return fallback;
    if (value < 0.0f) return 0.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

float sanitizePhCutoff(float value, float fallback) {
    if (!isfinite(value)) return fallback;
    if (value < 7.80f) return 7.80f;
    if (value > 8.80f) return 8.80f;
    return value;
}

void loadMode7DayNightSplit() {
    prefs.begin("doser-settings", true);
    mode7DayNightSplitEnabled = prefs.getBool("m7_split_en", mode7DayNightSplitEnabled);
    mode7DayNaohPct = sanitizePercent(prefs.getFloat("m7_day_naoh", mode7DayNaohPct), 0.0f);
    mode7DayAlkPct = sanitizePercent(prefs.getFloat("m7_day_alk", mode7DayAlkPct), 100.0f);
    mode7NightNaohPct = sanitizePercent(prefs.getFloat("m7_nite_naoh", mode7NightNaohPct), 100.0f);
    mode7NightAlkPct = sanitizePercent(prefs.getFloat("m7_nite_alk", mode7NightAlkPct), 0.0f);
    mode7NaohMaxPh = sanitizePhCutoff(prefs.getFloat("m7_naoh_ph", mode7NaohMaxPh), 8.45f);
    prefs.end();
    applyMode7DayNightSplitToEngine();
}

void saveMode7DayNightSplit() {
    prefs.begin("doser-settings", false);
    prefs.putBool("m7_split_en", mode7DayNightSplitEnabled);
    prefs.putFloat("m7_day_naoh", mode7DayNaohPct);
    prefs.putFloat("m7_day_alk", mode7DayAlkPct);
    prefs.putFloat("m7_nite_naoh", mode7NightNaohPct);
    prefs.putFloat("m7_nite_alk", mode7NightAlkPct);
    prefs.putFloat("m7_naoh_ph", mode7NaohMaxPh);
    prefs.end();
}

// v1 -> v2 migration: intentionally a no-op now. AIEngineV2/Allocator has no
// setMode7DayNightSplit()-shaped API -- the percentage split concept is
// replaced by Allocator.cpp's fixed lights-off pH-taper boost (see the
// block comment above runAiRecalculation() in the AI recalculation section
// for the full explanation). Kept as a function (rather than deleted
// outright) purely so loadMode7DayNightSplit() and any WebRoutes.cpp
// dashboard handlers that call it still compile unchanged; the Preferences
// values it used to push into the engine are simply no longer read by
// anything that doses.
void applyMode7DayNightSplitToEngine() {
    // Intentionally empty post-migration -- see comment above.
}


float sanitizeAiSafetyValue(float value, float fallback, float minVal, float maxVal) {
    if (!isfinite(value) || value < minVal) return fallback;
    if (value > maxVal) return maxVal;
    return value;
}

float defaultAiMaxKalkDayMl() { return scaledDefault300To1100(2500.0f, 35000.0f); }
float defaultAiMaxNaohDayMl() { return scaledDefault300To1100(100.0f, 1200.0f); }
float defaultAiMaxAlkDayMl() { return scaledDefault300To1100(500.0f, 2500.0f); }
float defaultAiMaxAlkRiseDkhDay() { return scaledDefault300To1100(1.50f, 2.00f); }
float defaultAiMaxMgCorrectionDayMl() { return scaledDefault300To1100(100.0f, 250.0f); }
float defaultAiMaxMgDayMl() { return scaledDefault300To1100(100.0f, 250.0f); }
float defaultAiMgDeadbandPpm() { return 25.0f; }

float prefFloatOrScaledDefault(Preferences& p, const char* key, float fallback) {
    return p.isKey(key) ? p.getFloat(key, fallback) : fallback;
}

void loadAiChemistrySafeties() {
    prefs.begin("doser-settings", true);

    // Existing saved Preferences always win. These calculated defaults are used
    // only for a brand-new customer/chip or a missing individual key.
    aiMaxKalkDayMl = sanitizeAiSafetyValue(prefFloatOrScaledDefault(prefs, "ai_max_kalk", defaultAiMaxKalkDayMl()), defaultAiMaxKalkDayMl(), 1.0f, 250000.0f);
    aiMaxNaohDayMl = sanitizeAiSafetyValue(prefFloatOrScaledDefault(prefs, "ai_max_naoh", defaultAiMaxNaohDayMl()), defaultAiMaxNaohDayMl(), 1.0f, 250000.0f);
    aiMaxAlkDayMl = sanitizeAiSafetyValue(prefFloatOrScaledDefault(prefs, "ai_max_alk", defaultAiMaxAlkDayMl()), defaultAiMaxAlkDayMl(), 1.0f, 250000.0f);
    aiMaxAlkRiseDkhDay = sanitizeAiSafetyValue(prefFloatOrScaledDefault(prefs, "ai_alk_rise", defaultAiMaxAlkRiseDkhDay()), defaultAiMaxAlkRiseDkhDay(), 0.05f, 5.0f);
    aiMaxCaRisePpmDay = sanitizeAiSafetyValue(prefs.getFloat("ai_ca_rise", 20.0f), 20.0f, 0.1f, 50.0f);
    aiMaxMgCorrectionDayMl = sanitizeAiSafetyValue(prefFloatOrScaledDefault(prefs, "ai_mg_corr", defaultAiMaxMgCorrectionDayMl()), defaultAiMaxMgCorrectionDayMl(), 0.0f, 250000.0f);
    aiMaxMgDayMl = sanitizeAiSafetyValue(prefFloatOrScaledDefault(prefs, "ai_max_mg", defaultAiMaxMgDayMl()), defaultAiMaxMgDayMl(), 1.0f, 250000.0f);
    aiMgDeadbandPpm = sanitizeAiSafetyValue(prefFloatOrScaledDefault(prefs, "ai_mg_dead", defaultAiMgDeadbandPpm()), defaultAiMgDeadbandPpm(), 0.0f, 200.0f);
    prefs.end();

    Serial.printf("AI CHEM SAFETIES LOADED: tank=%.1f gal maxKalk=%.2f maxNaOH=%.2f maxAlk=%.2f maxAlkRise=%.2f maxMgCorr=%.2f maxMg=%.2f mgDeadband=%.2f\n",
                  currentTankGallonsForDefaults(), aiMaxKalkDayMl, aiMaxNaohDayMl, aiMaxAlkDayMl, aiMaxAlkRiseDkhDay, aiMaxMgCorrectionDayMl, aiMaxMgDayMl, aiMgDeadbandPpm);
    logger.printf("AI CHEM SAFETIES LOADED: tank=%.1f gal maxKalk=%.2f maxNaOH=%.2f maxAlk=%.2f maxAlkRise=%.2f maxMgCorr=%.2f maxMg=%.2f mgDeadband=%.2f\n",
                  currentTankGallonsForDefaults(), aiMaxKalkDayMl, aiMaxNaohDayMl, aiMaxAlkDayMl, aiMaxAlkRiseDkhDay, aiMaxMgCorrectionDayMl, aiMaxMgDayMl, aiMgDeadbandPpm);

    applyAiChemistrySafetiesToEngine();
}

void saveAiChemistrySafeties() {
    prefs.begin("doser-settings", false);
    prefs.putFloat("ai_max_kalk", aiMaxKalkDayMl);
    prefs.putFloat("ai_max_naoh", aiMaxNaohDayMl);
    prefs.putFloat("ai_max_alk", aiMaxAlkDayMl);
    prefs.putFloat("ai_alk_rise", aiMaxAlkRiseDkhDay);
    prefs.putFloat("ai_ca_rise", aiMaxCaRisePpmDay);
    prefs.putFloat("ai_mg_corr", aiMaxMgCorrectionDayMl);
    prefs.putFloat("ai_max_mg", aiMaxMgDayMl);
    prefs.putFloat("ai_mg_dead", aiMgDeadbandPpm);
    prefs.end();
}

// v1 -> v2 migration: intentionally a no-op now. v1 cached these limits
// inside AIEngine via a setter; v2's rebuildAiChemicalDeclarations() and
// runAiRecalculation() read aiMaxKalkDayMl/aiMaxNaohDayMl/aiMaxAlkDayMl/
// aiMaxAlkRiseDkhDay/aiMaxCaRisePpmDay/aiMaxMgCorrectionDayMl/aiMaxMgDayMl
// directly from these same globals on every recalculation cycle, so there is
// nothing left to push into the engine ahead of time. aiMgDeadbandPpm has no
// v2 consumer at all yet (no equivalent deadband concept in Allocator.cpp).
// Kept as a function purely so loadAiChemistrySafeties() and any
// WebRoutes.cpp handlers calling it still compile unchanged.
void applyAiChemistrySafetiesToEngine() {
    // Intentionally empty post-migration -- see comment above.
}

const char* chemicalCapacityKey(int idx) {
    switch (idx) {
        case 0: return "cap1";
        case 1: return "cap2";
        case 2: return "cap3";
        case 3: return "cap4";
        default: return "cap1";
    }
}

const char* chemicalRemainingKey(int idx) {
    switch (idx) {
        case 0: return "rem1";
        case 1: return "rem2";
        case 2: return "rem3";
        case 3: return "rem4";
        default: return "rem1";
    }
}

void loadChemicalReservoirs() {
    prefs.begin("chem-levels", true);
    for (int i = 0; i < 4; ++i) {
        chemicalCapacityGal[i] = prefs.getFloat(chemicalCapacityKey(i), 0.0f);
        chemicalRemainingMl[i] = prefs.getFloat(chemicalRemainingKey(i), chemicalCapacityGal[i] * ML_PER_GALLON);

        if (!isfinite(chemicalCapacityGal[i]) || chemicalCapacityGal[i] < 0.0f) chemicalCapacityGal[i] = 0.0f;
        if (!isfinite(chemicalRemainingMl[i]) || chemicalRemainingMl[i] < 0.0f) chemicalRemainingMl[i] = 0.0f;

        float maxMl = chemicalCapacityGal[i] * ML_PER_GALLON;
        if (maxMl > 0.0f && chemicalRemainingMl[i] > maxMl) chemicalRemainingMl[i] = maxMl;
    }
    prefs.end();
}

void saveChemicalReservoirs() {
    prefs.begin("chem-levels", false);
    for (int i = 0; i < 4; ++i) {
        prefs.putFloat(chemicalCapacityKey(i), chemicalCapacityGal[i]);
        prefs.putFloat(chemicalRemainingKey(i), chemicalRemainingMl[i]);
    }
    prefs.end();
}

void recordChemicalDispense(int pumpIndex, float ml, const char* source) {
    if (pumpIndex < 0 || pumpIndex > 3 || ml <= 0.0f) return;

    // Single source of truth for daily dosing totals.
    // Count the actual mL returned by Doser::doseMl(), after any runtime safety cap.
    dailyDoseTotals[pumpIndex] += ml;
    saveDosingState();

    if (chemicalCapacityGal[pumpIndex] <= 0.0f) {
        Serial.printf("DOSE ACCOUNTING [%s]: P%d dispensed %.2f ml, today %.2f ml, reservoir disabled\n",
                      source ? source : "dose",
                      pumpIndex + 1,
                      ml,
                      dailyDoseTotals[pumpIndex]);
        logger.printf("DOSE ACCOUNTING [%s]: P%d dispensed %.2f ml, today %.2f ml, reservoir disabled\n",
                      source ? source : "dose",
                      pumpIndex + 1,
                      ml,
                      dailyDoseTotals[pumpIndex]);
        return;
    }

    chemicalRemainingMl[pumpIndex] -= ml;
    if (chemicalRemainingMl[pumpIndex] < 0.0f) chemicalRemainingMl[pumpIndex] = 0.0f;

    saveChemicalReservoirs();

    Serial.printf("CHEM LEVEL [%s]: P%d dispensed %.2f ml, today %.2f ml, remaining %.2f gal\n",
                  source ? source : "dose",
                  pumpIndex + 1,
                  ml,
                  dailyDoseTotals[pumpIndex],
                  chemicalRemainingMl[pumpIndex] / ML_PER_GALLON);
    logger.printf("CHEM LEVEL [%s]: P%d dispensed %.2f ml, today %.2f ml, remaining %.2f gal\n",
                  source ? source : "dose",
                  pumpIndex + 1,
                  ml,
                  dailyDoseTotals[pumpIndex],
                  chemicalRemainingMl[pumpIndex] / ML_PER_GALLON);
}


// struct dailyStats is now defined in lib/WebRoutes/WebRoutesShared.h
// (moved there so WebRoutes.cpp can see the type too). This is still the
// one real instance.
struct dailyStats dailyStats;

bool reportPushedToday = false;
String dailyAccountingDate = "";


bool isValidSystemMode(int mode) {
    return mode >= 0 && mode <= 2;
}

bool isValidDosingMode(int mode) {
    return mode >= 1 && mode <= 8;
}

bool isValidNotificationLevel(const String& level) {
    return level == "muted" || level == "severe" || level == "warning" || level == "info";
}

bool hasValidLiveChemistry() {
    return currentAlk > 0.0f && currentCa > 0.0f && currentMg > 0.0f && currentPh > 0.0f;
}

bool hasValidSavedManualChemistry() {
    return hasSavedManualTest &&
           lastLocalTest.alk > 0.0f &&
           lastLocalTest.ca  > 0.0f &&
           lastLocalTest.mg  > 0.0f &&
           lastLocalTest.ph  > 0.0f;
}

// §1/§3.2 "reduce required manual testing frequency over time (starting
// daily -> toward weekly)... actively prompts for a manual test if 7+ days
// have passed with no manual cross-check." Owner decision 2026-07-24: this
// applies ONLY to manual-only customers -- a tank with Apex/Trident
// automated testing (every few hours) has no need for this prompt at all,
// since it's already cross-checking constantly. Engine-side: tracks the
// data and computes the recommendation; dashboard (not built yet) is
// expected to read this and render the actual prompt UI.
//
// Real epoch time, not millis(): saveManualTestLocally()'s existing
// last_ts field stores millis()/1000 (uptime seconds), which resets to a
// stale, meaningless value across every reboot -- fine for whatever it was
// originally used for, but wrong for a multi-day "how long has it been"
// calculation. This uses a separate, correctly wall-clock-anchored field
// instead, added alongside (not replacing) the existing one.
uint32_t lastManualTestEpochSec = 0; // 0 = never recorded / not yet synced

// §1/§3.2 manual-test-prompt applicability. Confirmed working visually on
// reefDoser3 on 2026-07-25 (a temporary debug override forced this true for
// that check -- removed now that it's confirmed; see MIGRATION_NOTES.md-
// style history in git/chat log if this needs revisiting).
bool isManualOnlyTank() {
    return !apexEnabled && !useApexLogEmulatorForThisDevice();
}

// Conservative on purpose: the LEAST mature of Alk/Ca/Mg governs the
// recommended interval, not an average -- the system is only as proven as
// its least-confident parameter. Mg is excluded here in the common case
// where no active chemical touches it (see the earlier §5.2 sufficiency-
// check fix) -- an unaddressable parameter's permanently-low maturity
// would otherwise force this to always report "day 1" regardless of how
// well-proven Alk/Ca actually are.
//
// Uses provenMaturity(), not maturity(). Fixed 2026-07-27: maturity()
// legitimately decays between measurements (correct for its original
// dosing-caution purpose, see AI_EngineV2.h), but that meant a customer
// could watch this number go from 10% back to 0% a few hours after a good
// test, with no new bad data -- just time passing. For a customer-facing
// "the system has proven itself" signal, that's confusing and undermines
// trust in the recommendation. provenMaturity() is a genuine ratchet:
// tracks the best this filter has ever demonstrated, never regresses on
// its own.
float manualTestGoverningMaturity() {
    float m = fminf(ai.filters[P_ALK].provenMaturity(), ai.filters[P_CA].provenMaturity());
    bool mgAddressable = false;
    for (int i = 0; i < declaredChemicalCount; i++) {
        if (declaredChemicals[i].active && declaredChemicals[i].potencyMgPerMl != 0.0f) {
            mgAddressable = true;
            break;
        }
    }
    if (mgAddressable) m = fminf(m, ai.filters[P_MG].provenMaturity());
    return m;
}

// Fixed 2026-07-25: previously a smooth 1-7 day linear ramp with maturity --
// technically matched the spec's literal wording ("starting daily -> toward
// weekly") but didn't map onto three clean, nameable stages a customer can
// actually reason about ("test every 4 days" isn't daily, every-other-day,
// OR weekly). Owner's actual mental model is three discrete tiers, so this
// now returns one of exactly three values instead of a continuum.
// Thresholds (0.33/0.66 of maturity) are a reasonable starting split, not
// fleet-validated -- same caveat as every other derived-not-measured number
// in this codebase.
int recommendedManualTestIntervalDays() {
    float m = manualTestGoverningMaturity();
    if (m < 0.33f) return 1; // daily
    if (m < 0.66f) return 2; // every other day
    return 7;                // weekly
}

// Returns -1 if no manual test has ever been recorded with valid synced
// time (caller should treat that as "prompt immediately," not "0 days").
int daysSinceLastManualTest() {
    if (lastManualTestEpochSec == 0) return -1;
    time_t now = time(nullptr);
    if (now < 1700000000) return -1; // this device's own clock isn't synced yet either
    long deltaSec = (long)now - (long)lastManualTestEpochSec;
    if (deltaSec < 0) return -1; // clock stepped backward (fresh NTP sync); don't report nonsense
    return (int)(deltaSec / 86400L);
}

// ============================================================================
// v2 recalculation pipeline
// ----------------------------------------------------------------------------
// REMOVED (v1 -> v2 migration): enforceFastAlkPlanConversion() and
// enforceAllModeNetRecovery() are gone. Those were two of the "stacked
// override layers" MIGRATION_NOTES.md calls out by name — extra passes that
// ran AFTER AI_Engine's own calculation and could silently re-adjust a plan
// the engine had already produced (that's the exact Mode 7 bug class the v2
// spec was written to eliminate structurally). v2 replaces both of them with
// a single call: Allocator::solve() inside AIEngineV2::recalculate() builds
// the desired-correction vector, the weighted cross-effect matrix (including
// day/night + pH derating), runs one NNLS solve, and applies one uniform
// safety-cap scale-down. Nothing downstream is allowed to touch the plan
// again — see Allocator.h's precedence-order comment block.
//
// Net effect on behavior, called out explicitly rather than left implicit:
//   - The v1 "learned daily consumption + net rise" baseline-replacement
//     idea (enforceAllModeNetRecovery, fed by alkDemandStore/baselineKalkMlDay
//     etc.) has no direct v2 equivalent. v2's per-parameter Kalman filter
//     (level + trend) is meant to capture ongoing consumption the same way,
//     automatically, from repeated real measurements, WITHOUT a separately
//     maintained baseline number (MIGRATION_NOTES.md row "addBaselineDemand").
//     alkDemandStore/calciumDemandStore and baselineKalkMlDay/etc. are left
//     fully intact below (DemandLearning.h/.cpp is a separate lib this
//     migration does not touch, and may still use them for
//     dashboard/Firebase reporting) but they no longer feed dosing math.
//   - v1's customer-configurable Mode 7 day/night ALK-vs-NaOH split
//     PERCENTAGES (mode7DayNaohPct/mode7DayAlkPct/mode7NightNaohPct/
//     mode7NightAlkPct) have no v2 equivalent either: AIEngineV2 has no
//     setMode7DayNightSplit()-shaped API. v2's Allocator instead applies a
//     fixed, non-configurable lights-off assist (1.15x, capped by the same
//     pH taper) to any phSensitive chemical (Allocator.cpp). The percentage
//     Preferences/dashboard fields are left in place below (harmless to keep
//     reading/saving) but are no longer wired to anything that doses.
//   - v1's customer-configurable NaOH pH ceiling (mode7NaohMaxPh, default
//     8.45) is still loaded/saved for the dashboard, but Allocator.cpp
//     currently hardcodes its own ceiling (8.60) rather than accepting one
//     from the caller -- its own comment marks this as "until wired to the
//     per-tank Recommendable." That's a gap in the delivered engine, not
//     something this main.cpp-only migration can safely patch by guessing
//     at the intended API shape; flagging here so it isn't missed.
// ============================================================================

CoralLoad coralLoadFromBaselineString(const String& s) {
    if (s == "light") return CoralLoad::Light;
    if (s == "heavy") return CoralLoad::Heavy;
    if (s == "sps" || s == "sps_dominant") return CoralLoad::SPSDominant;
    return CoralLoad::Moderate; // "moderate", "custom", or unrecognized
}

// v1 exposed a per-mode hardware wiring table only through
// pumpKeyForPhysicalIndex() (used for buckets/Firebase aliasing). Reusing it
// here as the single source of truth for "which legacy chemical, if any, is
// on physical pump idx in the CURRENT dosingMode" avoids describing the same
// hardware wiring twice in two places that could drift apart.
int physicalPumpIndexForLegacySlot(LegacyChemSlot slot) {
    static const char* kSlotKey[kLegacyChemCount] = { "kalk", "afr", "alk", "cacl2", "naoh", "mg" };
    const char* wantKey = kSlotKey[slot];
    for (int idx = 0; idx < 4; idx++) {
        const char* key = pumpKeyForPhysicalIndex(idx);
        if (strcmp(key, wantKey) == 0) return idx;
        // Modes 4/5 wire CaCl2 through pumpKeyForPhysicalIndex's "ca" alias.
        if (slot == SLOT_CACL2 && strcmp(key, "ca") == 0) return idx;
    }
    return -1;
}

// Rebuilds AIEngineV2's chemical declarations from declaredChemicals[]
// (§5 free chemical declaration -- replaces the old mode/SlotSpec-keyed
// version). Call before every recalculate() -- cheap (<=4 declarations)
// and keeps this the single place a chemical add/edit/remove takes
// effect, instead of pushing individual setters into the engine from half
// a dozen call sites.
//
// INDEX CORRESPONDENCE IS LOAD-BEARING: ai.chemicals[i] must always match
// declaredChemicals[i] by index (same loop order, nothing skipped) so that
// DosingPlanV2::mlPerDay[i] (produced by ai.recalculate()) can be read
// back by the bucket scheduler as "declaredChemicals[i].pumpIndex should
// receive this many mL/day" without a second lookup. Do not skip inactive
// entries here -- add them with chem.active=false/maxMlPerDay=0 instead
// (matches how Allocator::solve() already treats an inactive chemical),
// or the index correspondence breaks and doses go to the wrong pump.
void rebuildAiChemicalDeclarations() {
    ai.numChemicals = 0;
    for (int i = 0; i < declaredChemicalCount; i++) {
        const DeclaredChemical& d = declaredChemicals[i];

        ChemicalDeclaration chem;
        strncpy(chem.name, d.name.c_str(), sizeof(chem.name) - 1);
        chem.potencyPerMl[P_ALK] = d.potencyAlkPerMl;
        chem.potencyPerMl[P_CA]  = d.potencyCaPerMl;
        chem.potencyPerMl[P_MG]  = d.potencyMgPerMl;
        // Fixed 2026-08-04: was unconditionally 0.0f for every chemical,
        // meaning the allocator's NNLS solve had no real number to weigh
        // pH against when choosing between chemicals for the same Alk/Ca
        // correction -- e.g. NaOH vs. baking soda for an Alk deficit, a
        // real reef-keeping tradeoff (baking soda has a documented mild,
        // sometimes slightly NEGATIVE pH effect at typical seawater pH;
        // NaOH's own real effect is comparatively small too, but that's a
        // relative judgment for whoever declares real per-chemical values
        // here, not something this line should assert). Now reads the
        // real per-chemical value (see DeclaredChemical::potencyPhPerMl) --
        // still 0.0f by default until explicitly set, same honest "not yet
        // declared" default every other potency field already uses.
        chem.potencyPerMl[P_PH]  = d.potencyPhPerMl;
        chem.phSensitive = d.phSensitive;
        chem.daytimeSuppressPercent = d.daytimeSuppressPercent;
        chem.active = d.active;
        chem.maxMlPerDay = d.active ? d.maxMlPerDay : 0.0f;

        // Seeded from stoichiometry (§5.2): full confidence on every
        // parameter this chemical actually moves. Confidence LEARNING from
        // real dose/response data is not implemented yet (see
        // MIGRATION_NOTES.md) -- unchanged by this rewrite.
        for (int p = 0; p < kNumParams; p++) {
            chem.confidence[p] = (chem.potencyPerMl[p] != 0.0f) ? 1.0f : 0.0f;
        }

        ai.addChemical(chem);
    }
}

int findDeclaredChemicalIndexById(const String& id) {
    for (int i = 0; i < declaredChemicalCount; i++) {
        if (declaredChemicals[i].id == id) return i;
    }
    return -1;
}

bool isPumpIndexTaken(int pumpIndex, const String& excludeId) {
    for (int i = 0; i < declaredChemicalCount; i++) {
        if (declaredChemicals[i].pumpIndex == pumpIndex && declaredChemicals[i].id != excludeId) {
            return true;
        }
    }
    return false;
}

String generateChemicalId() {
    // Simple, sufficiently-unique id: millis() + a monotonic counter. No
    // need for anything fancier at a hardware-capped max of 4 entries.
    static uint32_t counter = 0;
    counter++;
    return "chem_" + String(millis()) + "_" + String(counter);
}

static const char* CHEMICALS_CONFIG_PATH = "/chemicals.json";
static const char* CHEMICALS_CONFIG_TMP_PATH = "/chemicals.tmp";

bool loadDeclaredChemicals() {
    File f = LittleFS.open(CHEMICALS_CONFIG_PATH, "r");
    if (!f) return false; // not an error -- true first boot, or pre-migration

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("CHEMICALS CONFIG ERROR: parse failed: %s\n", err.c_str());
        logger.printf("CHEMICALS CONFIG ERROR: parse failed: %s\n", err.c_str());
        return false;
    }

    JsonArray arr = doc["chemicals"].as<JsonArray>();
    declaredChemicalCount = 0;
    for (JsonObject o : arr) {
        if (declaredChemicalCount >= kMaxDeclaredChemicals) break;
        DeclaredChemical& d = declaredChemicals[declaredChemicalCount];
        d.id               = o["id"]               | "";
        d.name             = o["name"]              | "";
        d.presetId         = o["presetId"]          | -1;
        d.potencyAlkPerMl  = o["potencyAlkPerMl"]   | 0.0f;
        d.potencyCaPerMl   = o["potencyCaPerMl"]    | 0.0f;
        d.potencyMgPerMl   = o["potencyMgPerMl"]    | 0.0f;
        // Added 2026-08-04: same "0.0f is honest, safe, and means not-yet-
        // declared" default as bucketThresholdMl/maxSingleDoseMl below --
        // an old chemicals.json saved before this field existed loads
        // safely, just with no pH cross-effect known for that chemical
        // until explicitly set (same as its actual real behavior always
        // was before this field existed at all).
        d.potencyPhPerMl   = o["potencyPhPerMl"]    | 0.0f;
        d.phSensitive      = o["phSensitive"]       | false;
        d.pumpIndex        = o["pumpIndex"]         | -1;
        d.maxMlPerDay      = o["maxMlPerDay"]       | 0.0f;
        // Added 2026-08-04: 0.0f default matches struct default and is
        // correctly treated as "not yet configured, fall back to this
        // pump's array-based value" by the getters in main.cpp -- so an
        // old chemicals.json saved before this field existed loads safely
        // with no behavior change until explicitly set.
        d.bucketThresholdMl = o["bucketThresholdMl"] | 0.0f;
        d.maxSingleDoseMl   = o["maxSingleDoseMl"]    | 0.0f;
        d.active           = o["active"]            | true;
        declaredChemicalCount++;
    }

    Serial.printf("CHEMICALS CONFIG LOADED: %d declared chemicals\n", declaredChemicalCount);
    logger.printf("CHEMICALS CONFIG LOADED: %d declared chemicals\n", declaredChemicalCount);
    return true;
}

bool saveDeclaredChemicals() {
    JsonDocument doc;
    JsonArray arr = doc["chemicals"].to<JsonArray>();
    for (int i = 0; i < declaredChemicalCount; i++) {
        const DeclaredChemical& d = declaredChemicals[i];
        JsonObject o = arr.add<JsonObject>();
        o["id"] = d.id;
        o["name"] = d.name;
        o["presetId"] = d.presetId;
        o["potencyAlkPerMl"] = d.potencyAlkPerMl;
        o["potencyCaPerMl"] = d.potencyCaPerMl;
        o["potencyMgPerMl"] = d.potencyMgPerMl;
        o["potencyPhPerMl"] = d.potencyPhPerMl;
        o["phSensitive"] = d.phSensitive;
        o["pumpIndex"] = d.pumpIndex;
        o["maxMlPerDay"] = d.maxMlPerDay;
        o["bucketThresholdMl"] = d.bucketThresholdMl;
        o["maxSingleDoseMl"] = d.maxSingleDoseMl;
        o["active"] = d.active;
    }

    File f = LittleFS.open(CHEMICALS_CONFIG_TMP_PATH, "w");
    if (!f) {
        Serial.println("CHEMICALS CONFIG ERROR: could not open temp file for writing");
        logger.println("CHEMICALS CONFIG ERROR: could not open temp file for writing");
        return false;
    }
    serializeJson(doc, f);
    f.close();

    if (LittleFS.exists(CHEMICALS_CONFIG_PATH)) {
        LittleFS.remove(CHEMICALS_CONFIG_PATH); // harmless if this races with a concurrent read; rename below is the atomic step
    }
    if (!LittleFS.rename(CHEMICALS_CONFIG_TMP_PATH, CHEMICALS_CONFIG_PATH)) {
        Serial.println("CHEMICALS CONFIG ERROR: rename failed");
        logger.println("CHEMICALS CONFIG ERROR: rename failed");
        return false;
    }
    return true;
}

// Builds declaredChemicals[]/declaredChemicalCount from a legacy
// dosingMode number. Reuses pumpKeyForPhysicalIndex() (the existing
// mode->pump->chemical-identity table) as the ONE-TIME/shim conversion
// source, rather than inventing yet a fourth copy of the same wiring
// knowledge (dashboard JS's DOSING_MODES and this function's predecessor,
// the SlotSpec table above, were already two independent copies -- see
// DASHBOARD_MIGRATION_PLAN.md). This function is NOT part of the ongoing
// dosing-execution path -- that's rebuildAiChemicalDeclarations()/the
// bucket scheduler now, both fully mode-agnostic. Used for: (1) the
// one-time boot migration when chemicals.json doesn't exist yet, and (2)
// the /api/dosing-mode compatibility shim while the old dashboard is
// still in use during the Phase 1-3 transition.
// Added 2026-08-04: pH-cost-per-dKH factors used to seed potencyPhPerMl
// below. Relative ordering only, not precise physical measurements --
// same "reasoned estimate, not fleet-validated" caveat as every other
// unvalidated constant in this codebase (see e.g. MathEngine's process-
// noise fraction, or Allocator.cpp's pH taper band).
//
// Grounded in real acid-base chemistry (NaOH and Ca(OH)2/kalkwasser are
// both strong bases with essentially complete dissociation, carbonate-
// based products like soda ash/AFR are meaningfully weaker bases) BUT
// kNaohPhCostPerDkh below is deliberately set LOW despite that raw
// chemistry fact -- corrected at the owner's direct, explicit real-world
// observation (2026-08-04): "[NaOH] has very little pH effect" in actual
// practice on their systems. Real-world per-mL pH impact depends heavily
// on the specific solution's concentration and how it's actually dosed
// (slowly, in small increments, day/night-routed), not just the raw
// chemistry of the base itself -- deferred to direct operator experience
// over abstract chemistry here, which is why NaOH sits at the LOW end
// despite being, in isolation, as strong a base as kalkwasser.
static constexpr float kNaohPhCostPerDkh = 0.005f;
static constexpr float kKalkPhCostPerDkh = 0.030f;
static constexpr float kCarbonateBasedPhCostPerDkh = 0.015f; // soda ash, AFR

void buildDeclaredChemicalsFromLegacyMode(int mode) {
    int savedMode = dosingMode;
    dosingMode = mode; // pumpKeyForPhysicalIndex() reads the global directly

    declaredChemicalCount = 0;
    for (int idx = 0; idx < 4 && declaredChemicalCount < kMaxDeclaredChemicals; idx++) {
        const char* key = pumpKeyForPhysicalIndex(idx);
        if (strcmp(key, "unused") == 0) continue;

        DeclaredChemical chem;
        chem.id = generateChemicalId();
        chem.pumpIndex = idx;
        chem.presetId = -1; // custom -- migrated from raw strength globals, not a named preset
        chem.active = true;
        chem.maxMlPerDay = pumpMaxDayMl[idx]; // matches this slot's existing pump-level cap

        if (strcmp(key, "kalk") == 0) {
            chem.name = "Kalkwasser";
            chem.potencyAlkPerMl = kalkStrengthDkhPerMl;
            chem.potencyCaPerMl = kalkStrengthDkhPerMl * CA_PPM_PER_DKH_KALK;
            // Added 2026-08-04, see potencyPhPerMl's comment block below
            // this if/else chain for the full reasoning and caveats.
            chem.potencyPhPerMl = kalkStrengthDkhPerMl * kKalkPhCostPerDkh;
            // Matches handlePostAddChemical()'s same default for the
            // Kalkwasser preset (see DeclaredChemical::nightFraction) --
            // V1 has always applied this 75/25 split to every Mode 7
            // customer's Kalk pump, this migration path should carry the
            // same default forward, not silently drop to flat/even just
            // because it went through a different code path.
            chem.nightFraction = 75.0f;
        } else if (strcmp(key, "afr") == 0) {
            chem.name = "AFR";
            chem.potencyAlkPerMl = afrStrengthDkhPerMl;
            chem.potencyCaPerMl = afrStrengthDkhPerMl * CA_PPM_PER_DKH_KALK;
            chem.potencyPhPerMl = afrStrengthDkhPerMl * kCarbonateBasedPhCostPerDkh;
        } else if (strcmp(key, "alk") == 0) {
            chem.name = "Alk (soda ash)";
            chem.potencyAlkPerMl = alkStrengthDkhPerMl;
            chem.potencyPhPerMl = alkStrengthDkhPerMl * kCarbonateBasedPhCostPerDkh;
        } else if (strcmp(key, "cacl2") == 0 || strcmp(key, "ca") == 0) {
            chem.name = "CaCl2";
            chem.potencyCaPerMl = cacl2StrengthPpmPerMl;
            // No potencyPhPerMl term -- CaCl2 delivers no Alk and is a
            // chemically neutral salt with no material pH side-effect.
        } else if (strcmp(key, "naoh") == 0) {
            chem.name = "NaOH";
            chem.potencyAlkPerMl = naohStrengthDkhPerMl;
            chem.phSensitive = true;
            chem.potencyPhPerMl = naohStrengthDkhPerMl * kNaohPhCostPerDkh;
        } else if (strcmp(key, "mg") == 0) {
            chem.name = "Mg";
            chem.potencyMgPerMl = mgStrengthPpmPerMl;
            // No potencyPhPerMl term -- no material pH effect declared for
            // the Mg products this migration path covers.
        } else {
            continue; // unrecognized key, skip defensively
        }

        declaredChemicals[declaredChemicalCount++] = chem;
    }

    dosingMode = savedMode; // restore -- this function must have no side effect on the live mode

    saveDeclaredChemicals();
    Serial.printf("CHEMICAL MIGRATION: built %d declared chemicals from legacy dosingMode=%d\n",
                  declaredChemicalCount, mode);
    logger.printf("CHEMICAL MIGRATION: built %d declared chemicals from legacy dosingMode=%d\n",
                  declaredChemicalCount, mode);
}

// Mirrors DosingPlanV2 (indexed by declared-chemical index, matching
// declaredChemicals[]/ai.chemicals[] exactly -- see
// rebuildAiChemicalDeclarations()'s comment on index correspondence) into
// two places:
//   1. planMlPerDayByIndex[] -- the real, mode-agnostic source of truth
//      the AI bucket scheduler reads to decide physical pump volume.
//   2. currentPlan's legacy named fields (.kalk/.afr/.alk/.cacl2/.naoh/.mg)
//      -- kept for now for Firebase/status reporting during the Phase 1-3
//      transition. Best-effort NAME matching (not slot-index assumption,
//      since a customer's declared chemical is no longer guaranteed to be
//      one of these six) -- degrades gracefully to 0 for a freely-named
//      chemical that doesn't match any legacy name. Remove once Phase 2's
//      pump-indexed reporting (plan/pump1..4) fully replaces this.
void syncLegacyPlanFromV2(const DosingPlanV2& plan) {
    for (int c = 0; c < kMaxDeclaredChemicals; c++) {
        planMlPerDayByIndex[c] = (plan.numChemicals > c) ? plan.mlPerDay[c] : 0.0f;
    }

    currentPlan.kalk = currentPlan.afr = currentPlan.alk = 0.0f;
    currentPlan.cacl2 = currentPlan.naoh = currentPlan.mg = 0.0f;
    for (int c = 0; c < declaredChemicalCount && c < kMaxDeclaredChemicals; c++) {
        String n = declaredChemicals[c].name;
        n.toLowerCase();
        float v = planMlPerDayByIndex[c];
        if (n.indexOf("kalk") >= 0) currentPlan.kalk += v;
        else if (n.indexOf("afr") >= 0 || n.indexOf("all-for-reef") >= 0 || n.indexOf("all for reef") >= 0) currentPlan.afr += v;
        else if (n.indexOf("naoh") >= 0 || n.indexOf("hydroxide") >= 0) currentPlan.naoh += v;
        // Fixed 2026-08-02: only matched "cacl2" / "calcium chloride" --
        // silently dropped any calcium chemical declared under a shorter
        // customer-facing name like plain "Calcium" (confirmed root cause
        // of reefDoser12 always logging cacl2=0.00 despite the allocator's
        // real solved x=100 for that slot -- caught via the temporary
        // Allocator diagnostic dump, name never matched any branch here,
        // so its value was silently discarded rather than added to any
        // legacy field). Added "calcium" as a standalone match. Order
        // matters: the "mg"/"magnesium" check below is moved ABOVE this
        // one so a hypothetical name like "Calcium/Magnesium Blend" still
        // routes to .mg first, not misclassified as pure calcium just
        // because "calcium" happens to appear earlier in the string.
        else if (n.indexOf("mg") >= 0 || n.indexOf("magnesium") >= 0) currentPlan.mg += v;
        else if (n.indexOf("cacl2") >= 0 || n.indexOf("calcium chloride") >= 0 || n.indexOf("calcium") >= 0) currentPlan.cacl2 += v;
        else if (n.indexOf("alk") >= 0 || n.indexOf("soda ash") >= 0 || n.indexOf("bicarbonate") >= 0) currentPlan.alk += v;
    }
    currentPlan.active = true;

    Serial.printf("V2 ALLOCATOR [%s]\n", plan.explanation);
    logger.printf("V2 ALLOCATOR [%s]\n", plan.explanation);
}

// Tracks elapsed real time between recalculation cycles so advanceTime()'s
// predict step (including the known-dose-effect input, §4.4/§4.5) gets a
// correct dt. Fixed 2026-07-24: ai.advanceTime() was never called anywhere
// in this file -- the Kalman filter's predict step (and today's
// control-input fix for the trend-estimation bug) had zero effect on real
// hardware despite being correct and simulator-verified, because nothing
// ever invoked it in production. Found from a field observation: Apex
// polls far more often than reefDoser2 actually completes a new Trident
// test, and v1 deliberately didn't feed unchanged readings back in --
// investigating why led directly to this gap.
static unsigned long lastAiRecalcMillis = 0;

// v1 -> v2 replacement for ai.calculateNextPlan(). Feeds the latest
// measurement into each parameter's Kalman filter, then lets
// AIEngineV2::recalculate() (one NNLS solve, one safety scale-down) produce
// the plan directly -- see the block comment above this section for exactly
// what v1 behavior does and does not carry over.
//
// isNewMeasurement gates the three ai.ingestMeasurement() calls only --
// advanceTime() (the predict step) always runs when real time has elapsed,
// regardless. Fixed 2026-07-24, same investigation as above: previously
// every call to this function fed whatever useAlk/useCa/useMg happened to
// be current into the Kalman filter, even when a config change (chemical
// added, safety cap edited) or a repeated Apex poll of the SAME unchanged
// Trident result triggered it -- inflating maturity/confidence from
// duplicate "observations" that were not actually new information. Pass
// true only from call sites that represent a genuinely new physical
// measurement (manual test entry, a real Apex/Trident result change);
// config-change and Hourly-reevaluation call sites default to false, which
// still recomputes the dosing plan against current settings/filter state,
// just without re-ingesting stale data as if it were fresh.
void runAiRecalculation(float useAlk, float useCa, float useMg, float usePh,
                         bool lightsOnNow, MeasurementSource measurementSource,
                         bool isNewMeasurement = false) {
    ai.tank.tankVolumeLiters = TANK_VOLUME_L;
    ai.tank.coralLoad = coralLoadFromBaselineString(baselineCoralLoad);

    loadChemicalStrengths();
    rebuildAiChemicalDeclarations();

    ai.targetAlkDkh.updateSuggestion(targetAlk);
    ai.targetCaPpm.updateSuggestion(targetCa);
    ai.targetMgPpm.updateSuggestion(targetMg);
    // Added 2026-08-05: the missing piece -- see the comment on the
    // targetPhLow/targetPhHigh declaration above for the full root-cause
    // explanation. Without this, filters[P_PH] could be correctly
    // initialized and ingesting real measurements (the earlier fix) and
    // desired[P_PH] would still stay permanently 0.0000, because the gap
    // calculation had nothing real to compare against.
    ai.targetPhLow.updateSuggestion(targetPhLow);
    ai.targetPhHigh.updateSuggestion(targetPhHigh);

    // §7 safety envelope -- the ONE place a rise-per-day gets capped now.
    ai.safety.maxAlkRisePerDayDkh.updateSuggestion(aiMaxAlkRiseDkhDay);
    ai.safety.maxCaRisePerDayPpm.updateSuggestion(aiMaxCaRisePpmDay);
    // TODO(migration): v1 had no direct Mg ppm/day rise cap, only an mL/day
    // cap (aiMaxMgCorrectionDayMl). Approximated via configured Mg strength;
    // confirm this matches the intended customer-facing behavior.
    ai.safety.maxMgRisePerDayPpm.updateSuggestion(aiMaxMgCorrectionDayMl * mgStrengthPpmPerMl);
    ai.safety.maxPhRisePerDay.updateSuggestion(0.0f); // 0 = uncapped; v1 never capped pH rise directly
    // Resolved 2026-07-24: was previously a hardcoded 8.60 inside
    // Allocator.cpp with a comment marking it "until wired to the per-tank
    // Recommendable" -- now it reads this customer-configurable value
    // (v1's own mode7NaohMaxPh, still loaded/saved for the dashboard
    // elsewhere in this file) instead of a fixed constant.
    ai.safety.naohPhCeiling.updateSuggestion(mode7NaohMaxPh);

    // Predict step: always runs when real time has elapsed, independent of
    // whether this cycle also has a new measurement. First-ever call (no
    // prior timestamp) skips advancing -- there's no meaningful elapsed
    // interval yet -- and just establishes the baseline. Capped at 1.0 day
    // per call (matches the bucket scheduler's own catch-up cap pattern
    // elsewhere in this file) so a long WiFi/reboot gap can't shove one
    // huge, unrealistic dt into the filter at once.
    unsigned long nowMs = millis();
    if (lastAiRecalcMillis != 0) {
        float dtDays = (float)(nowMs - lastAiRecalcMillis) / 86400000.0f;
        if (dtDays > 1.0f) dtDays = 1.0f;
        if (dtDays > 0.0f) ai.advanceTime(dtDays);
    }
    lastAiRecalcMillis = nowMs;

    if (isNewMeasurement) {
        if (isfinite(useAlk) && useAlk > 0.0f) ai.ingestMeasurement(P_ALK, useAlk, measurementSource);
        if (isfinite(useCa)  && useCa  > 0.0f) ai.ingestMeasurement(P_CA,  useCa,  measurementSource);
        if (isfinite(useMg)  && useMg  > 0.0f) ai.ingestMeasurement(P_MG,  useMg,  measurementSource);
        // Fixed 2026-08-05: usePh was accepted as a parameter to this
        // function but never actually ingested -- Alk/Ca/Mg all fed the
        // Kalman filter here, pH never did. filters[P_PH].initialized was
        // therefore never set true, which meant AIEngineV2::recalculate()'s
        // `if (!filters[p].initialized) continue;` guard silently zeroed
        // desired[P_PH] on every single cycle, regardless of how far real
        // pH drifted in either direction -- confirmed against a real
        // reefDoser12 log showing desired(...,pH,...)=0.0000 continuously
        // while pH itself drifted from 7.92 down to 7.76 over one night.
        // pH's own diurnal-swing correction (ingestMeasurement's
        // `if (p == P_PH)` branch in AI_EngineV2.cpp) already existed and
        // was simply never reached because this call was missing.
        if (isfinite(usePh) && usePh > 0.0f) ai.ingestMeasurement(P_PH, usePh, measurementSource);

        // Added 2026-08-04, §4.4: a measurement whose real outcome didn't
        // plausibly match what the currently-declared chemicals should
        // have produced used to just silently skip confidence learning,
        // with nothing surfaced anywhere -- exactly the failure mode the
        // spec's own words warn about: "a miss bigger than the mature-
        // phase correction cap would allow gets flagged as an anomaly to
        // the customer... rather than silently folded into the next
        // dose." This is that flag, finally real instead of silent.
        const char* paramNames[kNumParams] = {"Alk", "pH", "Ca", "Mg"};
        for (int p = 0; p < kNumParams; p++) {
            if (p == P_PH) continue; // pH is never ingested into a filter, see comment below
            if (ai.wasAnomaly((WaterParam)p)) {
                Serial.printf("ANOMALY DETECTED [%s]: measured result did not plausibly match what "
                              "the declared chemicals should have produced. Possible causes: an "
                              "unlogged water change or livestock addition (see §3.5 event logging), "
                              "pump drift, a chemical strength that's changed, or a bad reading.\n",
                              paramNames[p]);
                logger.printf("ANOMALY DETECTED [%s]: measured result did not plausibly match what "
                              "the declared chemicals should have produced. Possible causes: an "
                              "unlogged water change or livestock addition (see §3.5 event logging), "
                              "pump drift, a chemical strength that's changed, or a bad reading.\n",
                              paramNames[p]);
            }
        }
    }
    // pH is intentionally never ingested into a filter: v1 never dosed
    // toward a pH target, only gated NaOH on the CURRENT pH reading, which
    // recalculate()'s `usePh` argument already covers via phDerateWeight.

    // Added 2026-08-06: feeds the 7-day rolling learner's real, learned
    // daily consumption rate into recalculate() as an alternative
    // feedforward source -- "ready" requires both a full week of real
    // history AND the dashboard toggle being enabled, matching exactly
    // the same readiness check WebRoutes.cpp's GET endpoints already use
    // for display.
    bool alkLearnerReady = automaticDemandLearningEnabled && (alkDemandStore.count >= 7);
    bool caLearnerReady = automaticCalciumLearningEnabled && (calciumDemandStore.count >= 7);
    DosingPlanV2 plan = ai.recalculate(
        lightsOnNow, usePh);
    syncLegacyPlanFromV2(plan);
    currentPlan.active = true;

    // Added 2026-08-04, §4.2/§4.3: separate, clearly-labeled block rather
    // than folded into the existing ALLOCATOR DIAGNOSTIC (that block lives
    // in Allocator.cpp, not touched this session -- kept this independent
    // rather than risk reconstructing that file from memory). Only prints
    // a parameter once it has enough real history for the window in
    // question (hasWeekData/hasMonthData) -- a device with only hours of
    // real data genuinely has nothing meaningful to report yet, and
    // printing a trend computed from too little data would be worse than
    // not printing anything.
    {
        const char* paramNames[kNumParams] = {"Alk", "pH", "Ca", "Mg"};
        bool anyData = false;
        for (int p = 0; p < kNumParams; p++) {
            if (p == P_PH) continue; // never ingested into a filter, see comment above
            if (ai.hasWeekData((WaterParam)p) || ai.hasMonthData((WaterParam)p)) { anyData = true; break; }
        }
        if (anyData) {
            Serial.println("--- WEEK/MONTH TREND ---");
            logger.println("--- WEEK/MONTH TREND ---");
            for (int p = 0; p < kNumParams; p++) {
                if (p == P_PH) continue;
                WaterParam wp = (WaterParam)p;
                if (!ai.hasWeekData(wp) && !ai.hasMonthData(wp)) continue;
                char weekStr[24] = "not enough data";
                char monthStr[24] = "not enough data";
                if (ai.hasWeekData(wp)) snprintf(weekStr, sizeof(weekStr), "%.4f/day", ai.getWeekTrend(wp));
                if (ai.hasMonthData(wp)) snprintf(monthStr, sizeof(monthStr), "%.4f/day", ai.getMonthTrend(wp));
                bool drifting = ai.isDrifting(wp);
                Serial.printf("  %s: week=%s month=%s%s\n", paramNames[p], weekStr, monthStr,
                              drifting ? " [DRIFTING]" : "");
                logger.printf("  %s: week=%s month=%s%s\n", paramNames[p], weekStr, monthStr,
                              drifting ? " [DRIFTING]" : "");
            }
            Serial.println("--- END WEEK/MONTH TREND ---");
            logger.println("--- END WEEK/MONTH TREND ---");
        }
    }

    // §4.5 persistence: save learned Kalman state after every recalculation
    // driven by a real new measurement. This function only runs from
    // calculateAiFromBestChemistry() on the hourly/Apex-poll/manual-test
    // cadence (guarded upstream by isfinite/>0 checks on the ingested
    // values), not on some faster internal tick -- exactly the "meaningful
    // update, not every advanceTime() step" cadence called for in
    // AI_EngineV2.h's saveState() comment. Non-fatal if it fails; dosing
    // must not block on persistence succeeding.
    if (!ai.saveState()) {
        Serial.println("AI ENGINE V2: WARNING - saveState() failed (non-fatal, dosing continues).");
        logger.println("AI ENGINE V2: WARNING - saveState() failed (non-fatal, dosing continues).");
    }
}

void calculateAiFromBestChemistry(const char* sourceLabel, bool isNewMeasurement) {
    float useAlk = 0.0f;
    float useCa  = 0.0f;
    float useMg  = 0.0f;
    float usePh  = 0.0f;
    const char* chemistrySource = "none";
    MeasurementSource measurementSource = MeasurementSource::ManualTest;

    // Apex/live chemistry is authoritative when present. This prevents the
    // hourly AI task from using stale manual pH/Ca and creating bogus NaOH/CaCl2
    // corrections, then being overwritten by the next Apex pass.
    if (hasValidLiveChemistry()) {
        useAlk = currentAlk;
        useCa  = currentCa;
        useMg  = currentMg;
        usePh  = currentPh;
        chemistrySource = "live";
        measurementSource = MeasurementSource::ApexTrident;
    } else if (hasValidSavedManualChemistry()) {
        useAlk = lastLocalTest.alk;
        useCa  = lastLocalTest.ca;
        useMg  = lastLocalTest.mg;
        usePh  = lastLocalTest.ph;
        chemistrySource = "manual";
        measurementSource = MeasurementSource::ManualTest;
    } else {
        Serial.printf("AI skipped [%s]: no valid chemistry yet.\n", sourceLabel ? sourceLabel : "AI");
        logger.printf("AI skipped [%s]: no valid chemistry yet.\n", sourceLabel ? sourceLabel : "AI");
        return;
    }

    const bool lightsOnNow = isLightsOn();
    runAiRecalculation(useAlk, useCa, useMg, usePh, lightsOnNow, measurementSource, isNewMeasurement);

    Serial.printf("AI chemistry [%s]: source=%s Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f%s\n",
                  sourceLabel ? sourceLabel : "AI", chemistrySource, useAlk, useCa, useMg, usePh,
                  isNewMeasurement ? " [new measurement ingested]" : " [plan re-evaluated only]");
    logger.printf("AI chemistry [%s]: source=%s Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f%s\n",
                  sourceLabel ? sourceLabel : "AI", chemistrySource, useAlk, useCa, useMg, usePh,
                  isNewMeasurement ? " [new measurement ingested]" : " [plan re-evaluated only]");
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


// ---------------- PERSISTENT BOOT / RESET HISTORY ----------------
// Stored separately from the rotating Google Drive logger so the reason for a
// reboot survives even if the normal log upload was interrupted.
const char* BOOT_HISTORY_PATH = "/boot_history.log";
const size_t BOOT_HISTORY_MAX_BYTES = 16384;  // Keep the file small.
const size_t BOOT_HISTORY_KEEP_BYTES = 8192;  // Retain the newest half when trimmed.

void trimBootHistoryIfNeeded() {
    File in = LittleFS.open(BOOT_HISTORY_PATH, "r");
    if (!in) return;

    size_t fileSize = in.size();
    if (fileSize <= BOOT_HISTORY_MAX_BYTES) {
        in.close();
        return;
    }

    size_t start = fileSize > BOOT_HISTORY_KEEP_BYTES
                     ? fileSize - BOOT_HISTORY_KEEP_BYTES
                     : 0;
    in.seek(start, SeekSet);

    // Discard the first partial line after seeking into the middle of the file.
    if (start > 0) in.readStringUntil('\n');

    File out = LittleFS.open("/boot_history.tmp", "w");
    if (!out) {
        in.close();
        return;
    }

    uint8_t buffer[256];
    while (in.available()) {
        size_t count = in.read(buffer, sizeof(buffer));
        if (count == 0) break;
        out.write(buffer, count);
    }

    in.close();
    out.close();
    LittleFS.remove(BOOT_HISTORY_PATH);
    LittleFS.rename("/boot_history.tmp", BOOT_HISTORY_PATH);
}

void saveBootRecordToLittleFS(esp_reset_reason_t reason, uint64_t chipid) {
    trimBootHistoryIfNeeded();

    File file = LittleFS.open(BOOT_HISTORY_PATH, "a");
    if (!file) {
        Serial.println("BOOT HISTORY ERROR: unable to open /boot_history.log");
        return;
    }

    // Time is normally not synchronized this early in setup(), so use a clear
    // unsynced marker. Later uploaded logs still provide the wall-clock context.
    file.printf("BOOT reason=%s(%d) firmware=%s device=%s chip=%04X%08X "
                "freeHeap=%u minHeap=%u fsUsed=%u fsTotal=%u bootMillis=%lu time=not-synced\n",
                resetReasonToString(reason),
                (int)reason,
                FW_VERSION,
                deviceID.c_str(),
                (uint16_t)(chipid >> 32),
                (uint32_t)chipid,
                ESP.getFreeHeap(),
                ESP.getMinFreeHeap(),
                (unsigned)LittleFS.usedBytes(),
                (unsigned)LittleFS.totalBytes(),
                millis());
    file.close();
}

void printSavedBootHistory() {
    File file = LittleFS.open(BOOT_HISTORY_PATH, "r");

    Serial.println();
    Serial.println("================================");
    Serial.println("SAVED BOOT / RESET HISTORY");
    Serial.println("================================");

    if (!file) {
        Serial.println("No saved boot history found.");
        Serial.println("================================");
        return;
    }

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        Serial.println(line);
        WebSerial.println(line);
        logger.println(line);
    }

    file.close();
    Serial.println("================================");
    Serial.println();
    WebSerial.println("================================");
    logger.println("================================");
}

// REMOVED (v1 -> v2 migration): loadFastAlkLearnerState()/
// saveFastAlkLearnerStateIfChanged() are gone along with the rest of the
// fastAlk-learner machinery -- see the block comment above
// runAiRecalculation() for what replaces it. baselineKalkMlDay/
// baselineCacl2MlDay/baselineNaohMlDay/baselineMgMlDay globals and their
// Preferences load/save in loadLocalSettings() are left in place
// (DemandLearning/dashboard may still read them) but are no longer pushed
// into the AI engine.

// v1 -> v2 migration: intentionally a no-op now, same reasoning as
// applyMode7DayNightSplitToEngine()/applyAiChemistrySafetiesToEngine()
// above -- v2 has no setBaselineDemand()-shaped API, and
// rebuildAiChemicalDeclarations()/runAiRecalculation() don't consume a
// separate baseline-demand number at all (see the removal comment above).
// Kept as a function (not deleted outright) because WebRoutes.cpp
// (handlePostAiBaseline(), registerWebRoutes()) and
// DemandLearning.cpp (recordCompletedDayAndLearn()) still call it --
// deleting the function itself, rather than just what it used to do
// internally, breaks the link step for those two files without touching
// them. baselineKalkMlDay/etc. Preferences plumbing is untouched by this;
// only the push into the (now-gone) engine setter is removed.
void applyAiBaselineToEngine() {
    // Intentionally empty post-migration -- see comment above.
}


void loadChemicalRecipes() {
    prefs.begin("chem-recipe", true);
    recipeKalkGpg  = prefs.getFloat("gpg_kalk",  recipeKalkGpg);
    recipeAfrGpg   = prefs.getFloat("gpg_afr",   recipeAfrGpg);
    recipeAlkGpg   = prefs.getFloat("gpg_alk",   recipeAlkGpg);
    recipeNaohGpg  = prefs.getFloat("gpg_naoh",  recipeNaohGpg);
    recipeMgGpg    = prefs.getFloat("gpg_mg",    recipeMgGpg);
    recipeCacl2Gpg = prefs.getFloat("gpg_cacl2", recipeCacl2Gpg);
    recipeAfrType   = prefs.getString("type_afr",   recipeAfrType);
    recipeAlkType   = prefs.getString("type_alk",   recipeAlkType);
    recipeMgType    = prefs.getString("type_mg",    recipeMgType);
    recipeCacl2Type = prefs.getString("type_cacl2", recipeCacl2Type);
    prefs.end();

    if (!isfinite(recipeKalkGpg)  || recipeKalkGpg  < 0.0f) recipeKalkGpg = 12.0f;
    if (!isfinite(recipeAfrGpg)   || recipeAfrGpg   < 0.0f) recipeAfrGpg = 0.0f;
    if (!isfinite(recipeAlkGpg)   || recipeAlkGpg   < 0.0f) recipeAlkGpg = 100.0f;
    if (!isfinite(recipeNaohGpg)  || recipeNaohGpg  < 0.0f) recipeNaohGpg = 144.0f;
    if (!isfinite(recipeMgGpg)    || recipeMgGpg    < 0.0f) recipeMgGpg = 500.0f;
    if (!isfinite(recipeCacl2Gpg) || recipeCacl2Gpg < 0.0f) recipeCacl2Gpg = 250.0f;
}

void saveChemicalRecipes() {
    prefs.begin("chem-recipe", false);
    prefs.putFloat("gpg_kalk", recipeKalkGpg);
    prefs.putFloat("gpg_afr", recipeAfrGpg);
    prefs.putFloat("gpg_alk", recipeAlkGpg);
    prefs.putFloat("gpg_naoh", recipeNaohGpg);
    prefs.putFloat("gpg_mg", recipeMgGpg);
    prefs.putFloat("gpg_cacl2", recipeCacl2Gpg);
    prefs.putString("type_afr", recipeAfrType);
    prefs.putString("type_alk", recipeAlkType);
    prefs.putString("type_mg", recipeMgType);
    prefs.putString("type_cacl2", recipeCacl2Type);
    prefs.end();
}

void loadChemicalStrengths() {
    loadChemicalRecipes();
    prefs.begin("doser-settings", true);

    float kalkStrength  = prefs.getFloat("str_kalk",  kalkStrengthDkhPerMl);
    float afrStrength   = prefs.getFloat("str_afr",   afrStrengthDkhPerMl);
    float alkStrength   = prefs.getFloat("str_alk",   alkStrengthDkhPerMl);
    float naohStrength  = prefs.getFloat("str_naoh",  naohStrengthDkhPerMl);
    float mgStrength    = prefs.getFloat("str_mg",    mgStrengthPpmPerMl);
    float cacl2Strength = prefs.getFloat("str_cacl2", cacl2StrengthPpmPerMl);

    prefs.end();

    // v1 -> v2: strengths now live directly in main.cpp's own globals
    // (kalkStrengthDkhPerMl/etc.) instead of being cached inside AIEngine.
    kalkStrengthDkhPerMl  = kalkStrength;
    afrStrengthDkhPerMl   = afrStrength;
    alkStrengthDkhPerMl   = alkStrength;
    naohStrengthDkhPerMl  = naohStrength;
    mgStrengthPpmPerMl    = mgStrength;
    cacl2StrengthPpmPerMl = cacl2Strength;

    // Boot-safe proof of the ACTUAL strengths loaded into the AI.
    // Keep each line short and use Serial only. LocalFirstLogger may use a
    // fixed formatting buffer, so one long six-float logger.printf during
    // setup can corrupt memory before the dashboard/network is available.
    Serial.println("CHEMICAL STRENGTHS LOADED:");
    Serial.printf("  Kalk  = %.9f dKH/ml\n", kalkStrengthDkhPerMl);
    Serial.printf("  AFR   = %.9f dKH/ml\n", afrStrengthDkhPerMl);
    Serial.printf("  Alk   = %.9f dKH/ml\n", alkStrengthDkhPerMl);
    Serial.printf("  NaOH  = %.9f dKH/ml\n", naohStrengthDkhPerMl);
    Serial.printf("  Mg    = %.9f ppm/ml\n", mgStrengthPpmPerMl);
    Serial.printf("  CaCl2 = %.9f ppm/ml\n", cacl2StrengthPpmPerMl);
}

// handlePostChemicalStrengths() moved to lib/WebRoutes/WebRoutes.cpp


void tokenStatusCallback(TokenInfo info) {
    static unsigned long lastErrorLogMs = 0;
    static String lastErrorMessage = "";
    static bool readyLogged = false;

    if (info.status == token_status_error) {
        String errorMessage = info.error.message.c_str();
        unsigned long now = millis();

        // Log a new error immediately, but rate-limit repeated identical errors
        // to once every five minutes so Firebase token failures cannot flood
        // Serial, WebSerial, or the LittleFS logger.
        if (errorMessage != lastErrorMessage ||
            lastErrorLogMs == 0 ||
            now - lastErrorLogMs >= 300000UL) {

            Serial.printf("Firebase token error: %s\n", errorMessage.c_str());
            logger.printf("Firebase token error: %s\n", errorMessage.c_str());

            lastErrorMessage = errorMessage;
            lastErrorLogMs = now;
        }

        readyLogged = false;
        return;
    }

    if (info.status == token_status_ready) {
        // Only print once per successful ready period.
        if (!readyLogged) {
            Serial.println("Firebase token ready.");
            logger.println("Firebase token ready.");
            readyLogged = true;
        }

        lastErrorMessage = "";
        lastErrorLogMs = 0;
    }
}

String getTodayDateKey() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) {
        return "";
    }

    char today[11];
    strftime(today, sizeof(today), "%Y-%m-%d", &timeinfo);
    return String(today);
}

void clearDailyAccountingOnly(const char* reason) {
    memset(&dailyStats, 0, sizeof(dailyStats));
    for (int i = 0; i < 4; i++) {
        dailyDoseTotals[i] = 0.0f;
    }
    doser.resetDailyTotal();

    Serial.printf("DAILY ACCOUNTING RESET [%s]: stats and daily dose totals cleared.\n",
                  reason ? reason : "date-change");
    logger.printf("DAILY ACCOUNTING RESET [%s]: stats and daily dose totals cleared.\n",
                  reason ? reason : "date-change");
}

void rolloverDailyAccountingIfNeeded(const char* reason) {
    String today = getTodayDateKey();
    if (today.length() == 0) return;

    if (dailyAccountingDate.length() == 0) {
        dailyAccountingDate = today;
        return;
    }

    if (dailyAccountingDate != today) {
        Serial.printf("DAILY ACCOUNTING ROLLOVER: saved='%s' today='%s'. Clearing daily totals.\n",
                      dailyAccountingDate.c_str(), today.c_str());
        logger.printf("DAILY ACCOUNTING ROLLOVER: saved='%s' today='%s'. Clearing daily totals.\n",
                      dailyAccountingDate.c_str(), today.c_str());
        clearDailyAccountingOnly(reason ? reason : "date-rollover");
        dailyAccountingDate = today;
    }
}

void saveDosingState() {
    // Prevent old daily totals from being stamped with today's date after midnight.
    rolloverDailyAccountingIfNeeded("save-date-rollover");

    String today = getTodayDateKey();
    if (today.length() > 0) {
        dailyAccountingDate = today;
    }

    prefs.begin("doser-state", false);
    // Save buckets separately from daily accounting; buckets may safely survive reboot.
    prefs.putBytes("buckets", &pumpBuckets, sizeof(pumpBuckets));

    // Save Daily Stats and Daily Totals (for the graph), tied to a calendar date.
    prefs.putBytes("stats", &dailyStats, sizeof(dailyStats));
    prefs.putBytes("totals", &dailyDoseTotals, sizeof(dailyDoseTotals));
    if (dailyAccountingDate.length() > 0) {
        prefs.putString("date", dailyAccountingDate);
    }
    prefs.end();
}

void loadDosingState() {
    String today = getTodayDateKey();
    String savedDate = "";
    bool loadedBuckets = false;
    bool loadedDailyAccounting = false;

    prefs.begin("doser-state", true); // Read-only mode
    if (prefs.isKey("buckets")) {
        prefs.getBytes("buckets", &pumpBuckets, sizeof(pumpBuckets));
        loadedBuckets = true;
    }
    if (prefs.isKey("stats")) {
        prefs.getBytes("stats", &dailyStats, sizeof(dailyStats));
        loadedDailyAccounting = true;
    }
    if (prefs.isKey("totals")) {
        prefs.getBytes("totals", &dailyDoseTotals, sizeof(dailyDoseTotals));
        loadedDailyAccounting = true;
    }
    savedDate = prefs.getString("date", "");
    prefs.end();

    dailyAccountingDate = savedDate;

    // Critical fix: never carry yesterday's or undated daily totals into today.
    // Older firmware did not store a date, so an empty savedDate is treated as stale
    // once time is available. Buckets remain restored; only daily stats/totals clear.
    if (today.length() > 0 && loadedDailyAccounting && savedDate != today) {
        Serial.printf("DAILY ACCOUNTING DATE MISMATCH: saved='%s' today='%s'. Clearing stale totals.\n",
                      savedDate.c_str(), today.c_str());
        logger.printf("DAILY ACCOUNTING DATE MISMATCH: saved='%s' today='%s'. Clearing stale totals.\n",
                      savedDate.c_str(), today.c_str());
        clearDailyAccountingOnly("boot-date-mismatch");
        dailyAccountingDate = today;
        saveDosingState();
        return;
    }

    if (today.length() > 0 && dailyAccountingDate.length() == 0) {
        dailyAccountingDate = today;
        saveDosingState();
    }

    Serial.printf("Dosing state recovered from memory. buckets=%s daily=%s date=%s\n",
                  loadedBuckets ? "yes" : "no",
                  loadedDailyAccounting ? "yes" : "no",
                  dailyAccountingDate.length() ? dailyAccountingDate.c_str() : "none");
    logger.printf("Dosing state recovered from memory. buckets=%s daily=%s date=%s\n",
                  loadedBuckets ? "yes" : "no",
                  loadedDailyAccounting ? "yes" : "no",
                  dailyAccountingDate.length() ? dailyAccountingDate.c_str() : "none");
}

bool planPublishChangeIsSignificant(float oldVal, float newVal) {
    if (oldVal < 0.0f) return true;
    if (fabsf(newVal) < 0.01f && fabsf(oldVal) < 0.01f) return false;
    float denom = fabsf(oldVal) > 1.0f ? fabsf(oldVal) : 1.0f;
    return (fabsf(newVal - oldVal) / denom) >= PLAN_PUBLISH_CHANGE_FRACTION;
}

// Fixed 2026-08-02: previously published/logged from `currentPlan`'s six
// legacy named fields (.kalk/.afr/.alk/.cacl2/.naoh/.mg), populated by
// syncLegacyPlanFromV2()'s best-effort keyword matching against whatever
// name a customer typed for a chemical. That matching is inherently
// incomplete for freely-named chemicals (confirmed root cause of
// reefDoser12 silently dropping a chemical named "Calcium" -- it matched
// none of the six keyword branches and its real, correctly-solved dose was
// discarded with no warning). Root problem: a fixed vocabulary of legacy
// chemical-type names can never cover an arbitrary customer-typed name,
// no matter how many keywords get added -- any new name is one dashboard
// wizard entry away from hitting the same gap again.
//
// Real fix, not another keyword: publish BY PHYSICAL PUMP INDEX instead,
// same proven pattern WebRoutes.cpp's handleGetStatus() already uses for
// the local dashboard's `dosingMlPerDayByPump` field (which the dashboard
// JS already prefers over the legacy keys -- see Dashboard.h's
// `s.dosingMlPerDayByPump || s.dosingMlPerDay || ...` fallback chain).
// Pump index is never ambiguous -- it doesn't depend on what anyone typed,
// so it can't have this class of bug regardless of future chemical names.
// Also publishes pumpAssignments (chemical name per pump) so a Firebase/
// cloud-side viewer without local network access can still label pumps
// correctly, matching what /api/chemicals already gives the local
// dashboard for free.
//
// currentPlan / syncLegacyPlanFromV2() are left in place for now (other
// call sites -- baseline displays, chemicalStrengths, etc. -- still read
// them) but are no longer the source of truth for what actually gets
// dosed or published. Removing them entirely is the next cleanup step
// once nothing else depends on the six-name shape.
void publishAiPlanIfNeeded(const char* source, bool force) {
    if (apexTlsReservedOrCoolingDown()) return;
    if (WiFi.status() != WL_CONNECTED || !firebaseStarted || !Firebase.ready()) return;

    // Same computation as handleGetStatus()'s dosingMlPerDayByPump --
    // deliberately identical logic, not re-derived, so local and cloud
    // views can never silently disagree with each other again.
    float pumpPlanMl[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    String pumpChemName[4] = {"", "", "", ""};
    for (int c = 0; c < declaredChemicalCount && c < kMaxDeclaredChemicals; c++) {
        int pumpIdx = declaredChemicals[c].pumpIndex;
        if (pumpIdx >= 0 && pumpIdx < 4) {
            pumpPlanMl[pumpIdx] = planMlPerDayByIndex[c];
            pumpChemName[pumpIdx] = declaredChemicals[c].name;
        }
    }

    bool anyNonZero = false;
    for (int i = 0; i < 4; ++i) {
        if (fabsf(pumpPlanMl[i]) >= 0.01f) { anyNonZero = true; break; }
    }
    if (!anyNonZero && !force) return;

    bool changed = force;
    for (int i = 0; i < 4; ++i) {
        if (planPublishChangeIsSignificant(lastPublishedPumpMlDay[i], pumpPlanMl[i])) {
            changed = true;
            break;
        }
    }

    char today[11] = "";
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        strftime(today, sizeof(today), "%Y-%m-%d", &timeinfo);
        if (lastPlanPublishDate != String(today)) changed = true;
    }

    if (!changed) return;

    FirebaseJson json;
    json.set("pump1", pumpPlanMl[0]);
    json.set("pump2", pumpPlanMl[1]);
    json.set("pump3", pumpPlanMl[2]);
    json.set("pump4", pumpPlanMl[3]);
    json.set("updatedAt", (uint32_t)(millis() / 1000UL));
    json.set("source", source ? source : "AI");

    FirebaseJson assignJson;
    assignJson.set("pump1", pumpChemName[0]);
    assignJson.set("pump2", pumpChemName[1]);
    assignJson.set("pump3", pumpChemName[2]);
    assignJson.set("pump4", pumpChemName[3]);

    String path = "/devices/" + deviceID + "/state/dosingMlPerDayByPump";
    String assignPath = "/devices/" + deviceID + "/state/pumpAssignments";
    bool ok = Firebase.updateNode(writeFbdo, path.c_str(), json);
    if (ok) {
        // Best-effort -- assignment names change rarely, a missed write
        // here doesn't affect dosing or the mL/day values above at all.
        Firebase.updateNode(writeFbdo, assignPath.c_str(), assignJson);

        for (int i = 0; i < 4; ++i) lastPublishedPumpMlDay[i] = pumpPlanMl[i];
        if (strlen(today) > 0) lastPlanPublishDate = String(today);
        Serial.printf("Firebase plan OK [%s]: pump1(%s)=%.2f pump2(%s)=%.2f pump3(%s)=%.2f pump4(%s)=%.2f\n",
                      source ? source : "AI",
                      pumpChemName[0].c_str(), pumpPlanMl[0],
                      pumpChemName[1].c_str(), pumpPlanMl[1],
                      pumpChemName[2].c_str(), pumpPlanMl[2],
                      pumpChemName[3].c_str(), pumpPlanMl[3]);
        logger.printf("Firebase plan OK [%s]: pump1(%s)=%.2f pump2(%s)=%.2f pump3(%s)=%.2f pump4(%s)=%.2f\n",
                         source ? source : "AI",
                         pumpChemName[0].c_str(), pumpPlanMl[0],
                         pumpChemName[1].c_str(), pumpPlanMl[1],
                         pumpChemName[2].c_str(), pumpPlanMl[2],
                         pumpChemName[3].c_str(), pumpPlanMl[3]);
    } else {
        Serial.printf("Firebase plan skipped/failed [%s]: %s\n", source ? source : "AI", writeFbdo.errorReason().c_str());
        logger.printf("Firebase plan skipped/failed [%s]: %s\n", source ? source : "AI", writeFbdo.errorReason().c_str());
    }
}

// ============================================================================
// NIGHT-WEIGHTED DOSING (generalized across all dosing, any chemical)
// ----------------------------------------------------------------------------
// Direct generalization of V1's proven getKalkDayNightSliceMultiplier()/
// getBalancedKalkSliceMl(), which only ever applied to one hardcoded
// chemical. Same principle here, parameterized by each declared chemical's
// own nightFraction instead: keep the AI's daily total for that chemical
// completely unchanged (the allocator's NNLS solve and §7 safety caps are
// not touched at all), but redistribute WHEN within the day it's actually
// dispensed. nightFraction=50 (the default) reproduces today's flat, even
// distribution exactly; anything else weights slices toward day or night
// while still summing to the exact same daily total across all 144 slices.
float nightWeightedSliceMultiplier(float nightFraction, bool lightsOn) {
    int startHour = lightConfig.start;
    int endHour = lightConfig.end;
    if (startHour < 0 || startHour > 23) startHour = 8;
    if (endHour < 0 || endHour > 23) endHour = 20;

    int lightHours = endHour - startHour;
    if (lightHours <= 0) lightHours += 24;
    if (lightHours <= 0 || lightHours >= 24) lightHours = 12;
    const int darkHours = 24 - lightHours;

    float nf = nightFraction;
    if (!isfinite(nf) || nf < 0.0f) nf = 0.0f;
    if (nf > 100.0f) nf = 100.0f;
    const float nightDailyFraction = nf / 100.0f;
    const float dayDailyFraction = 1.0f - nightDailyFraction;

    const float lightSlices = static_cast<float>(lightHours * 6);
    const float darkSlices = static_cast<float>(darkHours * 6);
    if (lightSlices <= 0.0f || darkSlices <= 0.0f) return 1.0f; // defensive; shouldn't happen given the clamps above

    if (lightsOn) {
        return (dayDailyFraction * 144.0f) / lightSlices;
    }
    return (nightDailyFraction * 144.0f) / darkSlices;
}

float getNightWeightedSliceMl(float dailyMl, float nightFraction, bool lightsOn) {
    if (!isfinite(dailyMl) || dailyMl <= 0.0f) return 0.0f;
    return (dailyMl / 144.0f) * nightWeightedSliceMultiplier(nightFraction, lightsOn);
}

void addCurrentAiPlanToBuckets(const char* source, bool force) {
    // CRITICAL SCHEDULER FIX:
    // The daily plan is divided into 144 ten-minute slices. This function must
    // therefore be serviced continuously from loop(), not only when Apex polls
    // or the hourly AI calculation runs. Otherwise a 35,000 mL/day plan can
    // receive only ~24-48 slices/day and physically deliver only a fraction of
    // the requested amount.
    //
    // `force` is intentionally NOT allowed to create an extra slice. Manual or
    // Apex chemistry updates may recalculate the plan, but they must never add a
    // duplicate dose slice merely because new data arrived.
    (void)force;

    const unsigned long nowMs = millis();

    // Start the clock without adding an immediate slice at boot. Restored bucket
    // contents remain intact, and the first new slice is added after 10 minutes.
    if (lastAiBucketAddMs == 0) {
        lastAiBucketAddMs = nowMs;
        Serial.printf("AI BUCKET SCHEDULER STARTED [%s]: interval=%lu ms, slices/day=144\n",
                      source ? source : "Scheduler", AI_BUCKET_INTERVAL_MS);
        logger.printf("AI BUCKET SCHEDULER STARTED [%s]: interval=%lu ms, slices/day=144\n",
                      source ? source : "Scheduler", AI_BUCKET_INTERVAL_MS);
        return;
    }

    const unsigned long elapsedMs = nowMs - lastAiBucketAddMs;
    unsigned long elapsedSlices = elapsedMs / AI_BUCKET_INTERVAL_MS;
    if (elapsedSlices == 0) return;

    // Catch up short blocking delays (Firebase/TLS/logger) without allowing a
    // very long outage to create a dangerous multi-hour catch-up dump. Six
    // slices equals one hour. Any older skipped time is explicitly logged.
    static constexpr unsigned long MAX_BUCKET_CATCHUP_SLICES = 6UL;
    unsigned long slicesToAdd = elapsedSlices;
    if (slicesToAdd > MAX_BUCKET_CATCHUP_SLICES) {
        Serial.printf("AI BUCKET SCHEDULER LATE [%s]: elapsedSlices=%lu, safely limiting catch-up to %lu\n",
                      source ? source : "Scheduler", elapsedSlices, MAX_BUCKET_CATCHUP_SLICES);
        logger.printf("AI BUCKET SCHEDULER LATE [%s]: elapsedSlices=%lu, safely limiting catch-up to %lu\n",
                      source ? source : "Scheduler", elapsedSlices, MAX_BUCKET_CATCHUP_SLICES);
        slicesToAdd = MAX_BUCKET_CATCHUP_SLICES;

        // Drop time older than the bounded catch-up window. This prevents the
        // same stale intervals from being reconsidered on every loop pass.
        lastAiBucketAddMs = nowMs - (slicesToAdd * AI_BUCKET_INTERVAL_MS);
    }

    // §5 free chemical declaration: replaces the old 8-case
    // switch(dosingMode) default-case guard. No mode to be "invalid" now --
    // the only real failure state is having nothing declared at all.
    if (declaredChemicalCount == 0) {
        Serial.println("AI BUCKET SCHEDULER: no chemicals declared, nothing to dose.");
        logger.println("AI BUCKET SCHEDULER: no chemicals declared, nothing to dose.");
        return;
    }

    // Single snapshot for this whole batch, matching V1's same level of
    // precision -- lights state could in principle change mid-batch during
    // a large catch-up gap, but that's a rare edge case, not normal
    // operation, and not worth the complexity of per-slice light checks.
    const bool lightsOnForThisBatch = isLightsOn();

    for (unsigned long slice = 0; slice < slicesToAdd; ++slice) {
        // Each declared chemical carries its own pumpIndex now (always
        // assigned, see WebRoutesShared.h's DeclaredChemical comment), so
        // this loop works identically for any declared chemical set, not
        // just the eight legacy configurations. planMlPerDayByIndex[c] and
        // declaredChemicals[c] are index-aligned by construction (see
        // rebuildAiChemicalDeclarations()'s comment on why that matters).
        for (int c = 0; c < declaredChemicalCount && c < kMaxDeclaredChemicals; c++) {
            int pumpIdx = declaredChemicals[c].pumpIndex;
            if (pumpIdx < 0 || pumpIdx >= 4) continue; // defensive; shouldn't happen, pump is always assigned
            pumpBuckets[pumpIdx] += getNightWeightedSliceMl(
                planMlPerDayByIndex[c], declaredChemicals[c].nightFraction, lightsOnForThisBatch);
        }

        lastAiBucketAddMs += AI_BUCKET_INTERVAL_MS;
    }

    saveDosingState();

    Serial.printf("AI BUCKET SLICE [%s]: added=%lu plan(kalk=%.2f afr=%.2f alk=%.2f ca=%.2f naoh=%.2f mg=%.2f) buckets(P1=%.2f P2=%.2f P3=%.2f P4=%.2f)\n",
                  source ? source : "Scheduler",
                  slicesToAdd,
                  currentPlan.kalk,
                  currentPlan.afr,
                  currentPlan.alk,
                  currentPlan.cacl2,
                  currentPlan.naoh,
                  currentPlan.mg,
                  pumpBuckets[0], pumpBuckets[1], pumpBuckets[2], pumpBuckets[3]);
    logger.printf("AI BUCKET SLICE [%s]: added=%lu plan(kalk=%.2f afr=%.2f alk=%.2f ca=%.2f naoh=%.2f mg=%.2f) buckets(P1=%.2f P2=%.2f P3=%.2f P4=%.2f)\n",
                  source ? source : "Scheduler",
                  slicesToAdd,
                  currentPlan.kalk,
                  currentPlan.afr,
                  currentPlan.alk,
                  currentPlan.cacl2,
                  currentPlan.naoh,
                  currentPlan.mg,
                  pumpBuckets[0], pumpBuckets[1], pumpBuckets[2], pumpBuckets[3]);
}

// 2. Logic to determine Light State
bool isLightsOn() {
    if (lightConfig.source == 1) {
        if (useApexLogEmulatorForThisDevice()) {
            // Test emulator only supplies chemistry, not Apex outlet/light state.
            // Fall back to the local timer so reefDoser3 never calls real Apex in emulator mode.
            int hr = getLocalHour();
            return (hr >= lightConfig.start && hr < lightConfig.end);
        }
        return apex.getLightStatus();
    }
    int hr = getLocalHour();
    return (hr >= lightConfig.start && hr < lightConfig.end);
}


// Fixed 2026-08-05: getLocalTime(&timeinfo) with no explicit timeout
// argument defaults to a 5000ms internal blocking retry loop on ESP32 if
// the system clock hasn't synced via NTP yet -- which every log from this
// entire session showed ("time=not-synced"), since these devices spend
// long stretches offline or otherwise without a completed NTP sync. This
// function is called from isLightsOn(), which handleGetStatus() calls on
// every single /api/status request -- and a second, independent
// getLocalTime() call (in localDayKeyWithOffsetDays(), reached through
// daysSinceLastManualTest() inside the same request's manualTestPrompt
// block, confirmed applicable=true on this device) meant TWO stacked 5s
// blocks were plausible on one request -- closely matching an observed
// ~11 second single-request delay. All 6 getLocalTime() call sites in
// this file were changed to pass an explicit 10ms timeout: correct
// behavior is unchanged when the clock IS synced (returns true almost
// immediately either way), but a not-yet-synced clock now fails fast
// instead of blocking pointlessly -- waiting longer cannot make an NTP
// sync that hasn't happened suddenly complete synchronously anyway.
int getLocalHour() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo, 10)){
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

void prepareForOtaUpdate(const String& firmwareUrl) {
    if (!otaInProgress) {
        otaInProgress = true;
        otaStartedMs = millis();

        // Hard safety: OTA must never begin while a pump is allowed to keep running
        // or while the scheduler can start the next queued bucket.
        for (int i = 0; i < 4; i++) {
            calibrationRunActive[i] = false;
            calibrationRunUntilMs[i] = 0;
        }
        triggerEmergencyStop("OTA update started; dosing blocked until reboot", "OTA");

        Serial.println("OTA SAFETY: all pumps forced OFF; dosing blocked until reboot.");
        Serial.print("OTA SAFETY: target URL = ");
        Serial.println(firmwareUrl);

        logger.println("OTA SAFETY: all pumps forced OFF; dosing blocked until reboot.");
        logger.print("OTA SAFETY: target URL = ");
        logger.println(firmwareUrl);
    } else {
        doser.stopAllPumps();
    }
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

    // Persistent OTA guard:
    // If Firebase failed to clear /commands/ota before the reboot, the next boot
    // receives the same command in the root stream snapshot and can OTA again.
    // Store the OTA URL before starting the update so the next firmware can ignore
    // that stale command until it is successfully cleared.
    prefs.begin("ota-guard", false);
    bool otaPending = prefs.getBool("pending", false);
    String pendingUrl = prefs.getString("url", "");
    String pendingFromFw = prefs.getString("from_fw", "");
    prefs.end();

    if (otaPending && pendingUrl == firmwareUrl) {
        // If the running firmware changed since the previous OTA began, that OTA
        // succeeded and this is only the stale Firebase command left behind.
        if (pendingFromFw.length() > 0 && pendingFromFw != String(FW_VERSION)) {
            Serial.println("OTA ignored: previous OTA succeeded; clearing stale duplicate command.");
            Serial.print("Previous OTA started from FW: ");
            Serial.println(pendingFromFw);
            Serial.print("Current FW: ");
            Serial.println(FW_VERSION);

            logger.println("OTA ignored: previous OTA succeeded; clearing stale duplicate command.");
            logger.print("Previous OTA started from FW: ");
            logger.println(pendingFromFw);
            logger.print("Current FW: ");
            logger.println(FW_VERSION);

            String otaCommandPath = "/devices/" + deviceID + "/commands/ota";
            if (Firebase.deleteNode(writeFbdo, otaCommandPath.c_str())) {
                Serial.println("Stale OTA command cleared from Firebase.");
                logger.println("Stale OTA command cleared from Firebase.");

                prefs.begin("ota-guard", false);
                prefs.putBool("pending", false);
                prefs.remove("url");
                prefs.remove("from_fw");
                prefs.end();
            } else {
                Serial.print("Stale OTA command clear failed; keeping OTA guard active: ");
                Serial.println(writeFbdo.errorReason());
                logger.print("Stale OTA command clear failed; keeping OTA guard active: ");
                logger.println(writeFbdo.errorReason());
            }

            return false;
        }

        // The firmware version did not change, so the previous OTA failed or never
        // completed. Clear only the local guard and allow this command to retry.
        Serial.println("OTA retry allowed: previous attempt did not change firmware.");
        Serial.print("Previous OTA started from FW: ");
        Serial.println(pendingFromFw);
        Serial.print("Current FW: ");
        Serial.println(FW_VERSION);

        logger.println("OTA retry allowed: previous attempt did not change firmware.");
        logger.print("Previous OTA started from FW: ");
        logger.println(pendingFromFw);
        logger.print("Current FW: ");
        logger.println(FW_VERSION);

        prefs.begin("ota-guard", false);
        prefs.putBool("pending", false);
        prefs.remove("url");
        prefs.remove("from_fw");
        prefs.end();
    }

    Serial.print("OTA Triggered for ");
    Serial.println(deviceID);
    Serial.print("Starting OTA Update from: ");
    Serial.println(firmwareUrl);

    logger.print("OTA Triggered for ");
    logger.println(deviceID);
    logger.print("Starting OTA Update from: ");
    logger.println(firmwareUrl);

    emergencyStopBeforeOta = emergencyStop;
    prepareForOtaUpdate(firmwareUrl);

    // Save guard before any network call or reboot-risky work.
    prefs.begin("ota-guard", false);
    prefs.putBool("pending", true);
    prefs.putString("url", firmwareUrl);
    prefs.putString("from_fw", FW_VERSION);
    prefs.end();

    // Make OTA one-shot: clear the Firebase command before starting OTA so
    // stream reconnects do not replay the same /commands/ota node forever.
    //
    // Added 2026-08-06: cached as static -- this path is identical every
    // single time this function runs (deviceID never changes after boot),
    // so rebuilding it via fresh String concatenation on every call was
    // pure unnecessary heap churn, right in a function already shown to
    // crash under low memory. One small, free reduction in allocation
    // pressure in exactly the place it matters most.
    static String otaCommandPath = "/devices/" + deviceID + "/commands/ota";
    if (Firebase.deleteNode(writeFbdo, otaCommandPath.c_str())) {
        Serial.println("OTA command cleared from Firebase.");
        logger.println("OTA command cleared from Firebase.");
    } else {
        Serial.print("OTA command clear failed: ");
        Serial.println(writeFbdo.errorReason());
        logger.print("OTA command clear failed: ");
        logger.println(writeFbdo.errorReason());
    }

    // Added 2026-08-06: diagnostic heap logging plus a defensive minimum-heap
    // gate, right at the exact point a real device (reefDoser12) was
    // confirmed -- via a real serial capture -- to silently reset with no
    // error message at all, twice, always right in this same narrow window
    // between clearing the OTA command and stopping the Firebase stream.
    // minHeap readings nearby that failure were as low as ~38KB, which is
    // genuinely risky territory for the SSL/TLS work both deleteNode() just
    // did and endStream()/triggerEmergencyStop()'s own Firebase calls are
    // about to do -- ESP32's TLS stack commonly needs 40-50KB+ of
    // CONTIGUOUS free heap for a single handshake. This does not prevent
    // low heap from happening, but if it's already low right here, briefly
    // yielding gives other tasks a chance to free memory first, instead of
    // immediately piling more SSL-heavy work on top of an already-tight
    // heap. The exact numbers are logged either way, so the NEXT
    // occurrence (if any) will show precisely what heap looked like at
    // this exact moment, not an approximation from a nearby diagnostic
    // line seconds away.
    uint32_t heapBeforeStreamStop = ESP.getFreeHeap();
    Serial.printf("OTA HEAP CHECK: freeHeap=%u minFreeHeap=%u (before endStream)\n",
                  heapBeforeStreamStop, (uint32_t)ESP.getMinFreeHeap());
    logger.printf("OTA HEAP CHECK: freeHeap=%u minFreeHeap=%u (before endStream)\n",
                  heapBeforeStreamStop, (uint32_t)ESP.getMinFreeHeap());

    static const uint32_t OTA_MIN_SAFE_HEAP_BYTES = 45000UL;
    if (heapBeforeStreamStop < OTA_MIN_SAFE_HEAP_BYTES) {
        Serial.printf("OTA HEAP LOW: %u bytes free, below %u safety floor -- yielding briefly before continuing.\n",
                      heapBeforeStreamStop, OTA_MIN_SAFE_HEAP_BYTES);
        logger.printf("OTA HEAP LOW: %u bytes free, below %u safety floor -- yielding briefly before continuing.\n",
                      heapBeforeStreamStop, OTA_MIN_SAFE_HEAP_BYTES);
        for (int i = 0; i < 5; ++i) {
            esp_task_wdt_reset();
            delay(200);
            yield();
        }
        uint32_t heapAfterWait = ESP.getFreeHeap();
        Serial.printf("OTA HEAP CHECK: freeHeap=%u after brief yield (was %u)\n", heapAfterWait, heapBeforeStreamStop);
        logger.printf("OTA HEAP CHECK: freeHeap=%u after brief yield (was %u)\n", heapAfterWait, heapBeforeStreamStop);
    }

    // Stop the active Firebase stream before beginning the manifest and binary
    // HTTP transactions. This prevents stream callbacks/SSL recovery from running
    // alongside the OTA download and flash write.
    Serial.println("OTA TRANSACTION: stopping Firebase stream before download.");
    logger.println("OTA TRANSACTION: stopping Firebase stream before download.");
    Firebase.endStream(streamFbdo);

    // Reassert the physical safety state immediately before entering OTA.
    triggerEmergencyStop("OTA download/write transaction active", "OTA");
    esp_task_wdt_reset();
    delay(100);
    yield();

    Serial.println("OTA TRANSACTION: exclusive OTA download/write starting now.");
    logger.println("OTA TRANSACTION: exclusive OTA download/write starting now.");

    // A successful OTA reboots inside updateFirmware() and never returns here.
    // If this call returns, validation/download/write failed or was rejected.
    ota.updateFirmware(firmwareUrl, deviceID, BUILD_EXPECTED_MAC, BUILD_EXPECTED_CHIP);

    otaInProgress = false;
    otaStartedMs = 0;
    emergencyStop = emergencyStopBeforeOta;
    doser.stopAllPumps();

    // Do not leave a failed attempt marked as pending, or the next valid retry
    // would be rejected as a stale duplicate.
    prefs.begin("ota-guard", false);
    prefs.putBool("pending", false);
    prefs.remove("url");
    prefs.remove("from_fw");
    prefs.end();

    // OTA failed/returned, so restore the Firebase command stream before normal
    // operation resumes. A successful OTA never reaches this code.
    String streamPath = "/devices/" + deviceID + "/commands";
    if (Firebase.beginStream(streamFbdo, streamPath.c_str())) {
        Firebase.setStreamCallback(streamFbdo, streamCallback, streamTimeoutCallback);
        Serial.println("OTA FAILURE RECOVERY: Firebase stream restored.");
        logger.println("OTA FAILURE RECOVERY: Firebase stream restored.");
    } else {
        Serial.print("OTA FAILURE RECOVERY: Firebase stream restore failed: ");
        Serial.println(streamFbdo.errorReason());
        logger.print("OTA FAILURE RECOVERY: Firebase stream restore failed: ");
        logger.println(streamFbdo.errorReason());
    }

    Serial.println("OTA returned without reboot; OTA block cleared and normal operation restored.");
    logger.println("OTA returned without reboot; OTA block cleared and normal operation restored.");
    return false;
}
void serviceOtaCommandFallback() {
    static unsigned long lastOtaPollMs = 0;
    const unsigned long OTA_FALLBACK_POLL_MS = 30000UL;

    if (pendingOtaRequested || otaInProgress) return;
    if (apexTlsReservedOrCoolingDown()) return;
    if (WiFi.status() != WL_CONNECTED || !firebaseStarted || !Firebase.ready()) return;

    unsigned long nowMs = millis();
    if (lastOtaPollMs != 0 && nowMs - lastOtaPollMs < OTA_FALLBACK_POLL_MS) return;
    lastOtaPollMs = nowMs;

    String otaPath = "/devices/" + deviceID + "/commands/ota";
    if (!Firebase.getJSON(writeFbdo, otaPath.c_str())) {
        // A missing/null command is normal. Only log real Firebase errors.
        String reason = writeFbdo.errorReason();
        if (reason.length() > 0 && reason != "path not exist") {
            static unsigned long lastErrorLogMs = 0;
            if (nowMs - lastErrorLogMs > 300000UL) {
                lastErrorLogMs = nowMs;
                Serial.printf("OTA FALLBACK POLL: read failed: %s\n", reason.c_str());
                logger.printf("OTA FALLBACK POLL: read failed: %s\n", reason.c_str());
            }
        }
        return;
    }

    String targetUrl = "";
    String type = writeFbdo.dataType();

    if (type == "json") {
        FirebaseJson &json = writeFbdo.jsonObject();
        FirebaseJsonData result;
        if (json.get(result, "url")) {
            targetUrl = result.stringValue;
        }
    } else if (type == "string") {
        targetUrl = writeFbdo.stringData();
    }

    targetUrl.trim();
    if (targetUrl.length() == 0) return;

    pendingOtaUrl = targetUrl;
    pendingOtaSource = "30-second fallback poll";
    pendingOtaRequested = true;

    Serial.println("OTA FALLBACK POLL: command found and queued.");
    logger.println("OTA FALLBACK POLL: command found and queued.");
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
            if (!pendingOtaRequested && !otaInProgress) {
                pendingOtaUrl = res.stringValue;
                pendingOtaUrl.trim();
                pendingOtaSource = "root snapshot";
                pendingOtaRequested = pendingOtaUrl.length() > 0;

                Serial.println("OTA found inside root snapshot; queued for loop execution.");
                logger.println("OTA found inside root snapshot; queued for loop execution.");
            } else {
                Serial.println("OTA root snapshot ignored: OTA already queued/in progress.");
                logger.println("OTA root snapshot ignored: OTA already queued/in progress.");
            }
        } else {
            Serial.println("Root stream snapshot received; no ota/url found.");
            logger.println("Root stream snapshot received; no ota/url found.");

            // The previous OTA command is gone, so clear any local duplicate guard.
            prefs.begin("ota-guard", false);
            prefs.putBool("pending", false);
            prefs.remove("url");
            prefs.remove("from_fw");
            prefs.end();
        }
        return;
    }

// ... Inside main.cpp -> void streamCallback(StreamData data) ...

// ... Inside main.cpp -> void streamCallback(StreamData data) ...

    if (data.dataPath() == "/ota") {
        String targetUrl = "";

        // Accept either {"url":"..."} or a direct string URL.
        if (data.dataType() == "json") {
            FirebaseJson &json = data.jsonObject();
            FirebaseJsonData res;
            if (json.get(res, "url")) {
                targetUrl = res.stringValue;
            }
        } else if (data.dataType() == "string") {
            targetUrl = data.stringData();
        }

        targetUrl.trim();

        if (targetUrl.length() > 0 && !pendingOtaRequested && !otaInProgress) {
            pendingOtaUrl = targetUrl;
            pendingOtaSource = "/ota stream";
            pendingOtaRequested = true;

            Serial.println("OTA command queued for safe execution from loop.");
            logger.println("OTA command queued for safe execution from loop.");
        } else if (targetUrl.length() == 0) {
            Serial.println("OTA stream update ignored: URL missing.");
            logger.println("OTA stream update ignored: URL missing.");
        } else {
            Serial.println("OTA stream update ignored: OTA already queued/in progress.");
            logger.println("OTA stream update ignored: OTA already queued/in progress.");
        }
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

        // A Firebase dashboard manual test must become the same live source of
        // truth as a test entered on the local dashboard. The previous code only
        // recalculated the AI plan, leaving currentAlk/currentCa/currentMg/currentPh
        // unchanged, so both dashboards continued showing stale values.
        if (isfinite(mAlk) && mAlk >= 4.0f && mAlk <= 15.0f &&
            isfinite(mCa)  && mCa  >= 250.0f && mCa <= 700.0f &&
            isfinite(mMg)  && mMg  >= 800.0f && mMg <= 1800.0f &&
            isfinite(mPh)  && mPh  >= 6.50f && mPh <= 9.00f) {

            currentAlk = mAlk;
            currentCa  = mCa;
            currentMg  = mMg;
            currentPh  = mPh;
            saveManualTestLocally(mAlk, mCa, mMg, mPh);
            hasSavedManualTest = true;

            acceptNewChemistryMeasurement("CloudManual", "", true);
            calculateAiFromBestChemistry("CloudManual", true);
            addCurrentAiPlanToBuckets("CloudManual", true);
            publishAiPlanIfNeeded("CloudManual", true);

            bool mirrored = mirrorStatusToFirebase();
            Serial.printf(
                "WATER PARAMETERS UPDATED [CloudManual]: Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f Firebase=%s\n",
                currentAlk, currentCa, currentMg, currentPh,
                mirrored ? "OK" : "PENDING"
            );
            logger.printf(
                "WATER PARAMETERS UPDATED [CloudManual]: Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f Firebase=%s\n",
                currentAlk, currentCa, currentMg, currentPh,
                mirrored ? "OK" : "PENDING"
            );
        } else {
            Serial.println("Cloud manual chemistry ignored: invalid or incomplete values.");
            logger.println("Cloud manual chemistry ignored: invalid or incomplete values.");
        }
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

    // §3.2 manual-test-prompt feature: only record if this device's clock
    // is actually synced yet (see daysSinceLastManualTest()'s comment) --
    // otherwise leave whatever was last persisted alone rather than write
    // a meaningless pre-NTP timestamp over a real one.
    time_t nowEpoch = time(nullptr);
    if (nowEpoch > 1700000000) {
        lastManualTestEpochSec = (uint32_t)nowEpoch;
        prefs.putUInt("mtest_epoch", lastManualTestEpochSec);
    }
    prefs.end();

    lastLocalTest.alk = alk;
    lastLocalTest.ca = ca;
    lastLocalTest.mg = mg;
    lastLocalTest.ph = ph;
}

void loadManualTestLocally() {
    prefs.begin("doser-truth", true);

    unsigned long lastTs = prefs.getULong("last_ts", 0);
    lastManualTestEpochSec = prefs.getUInt("mtest_epoch", 0);

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

// §8/§8.5 setup wizard persistence.
void loadSetupWizardCompleted() {
    prefs.begin("doser-settings", true);
    setupWizardCompleted = prefs.getBool("wizard_done", false);
    prefs.end();
}

void saveSetupWizardCompleted(bool completed) {
    setupWizardCompleted = completed;
    prefs.begin("doser-settings", false);
    prefs.putBool("wizard_done", completed);
    prefs.end();
    Serial.printf("SETUP WIZARD: marked %s\n", completed ? "complete" : "incomplete (reset)");
    logger.printf("SETUP WIZARD: marked %s\n", completed ? "complete" : "incomplete (reset)");
}

// Shows the wizard for a genuinely fresh/never-actually-used device --
// distinguishing "has declared chemicals" from "has actually been used" is
// the important part. Fixed 2026-07-25: dosingMode has a compiled-in
// default (not loaded from NVS, so it survives a full flash erase), which
// makes the legacy migration silently create 1 declared chemical from that
// default before this check ever runs -- so a fully-wiped test chip still
// showed declaredChemicalCount > 0 and never triggered the wizard. A real,
// already-configured customer has both declared chemicals AND a real
// manual test on record; a device with only the auto-migrated default and
// zero test history is still fresh, whatever declaredChemicalCount says.
bool shouldShowSetupWizard() {
    if (setupWizardCompleted) return false;
    if (declaredChemicalCount > 0 && hasSavedManualTest) return false;
    return true;
}

// §8.5 chemistry targets persistence. Fixed 2026-07-25: previously these
// were hardcoded compile-time constants with literally no customer-facing
// way to change them -- a real functional gap, not just a missing wizard
// step. targetAlk/targetCa/targetMg's own initializers above remain the
// fallback defaults for a device that's never had this saved.
void loadChemistryTargets() {
    prefs.begin("doser-settings", true);
    targetAlk = prefs.getFloat("tgt_alk", targetAlk);
    targetCa  = prefs.getFloat("tgt_ca", targetCa);
    targetMg  = prefs.getFloat("tgt_mg", targetMg);
    // Added 2026-08-05: loaded the same way as the other three -- falls
    // back to the 8.0/8.4 defaults declared above if never explicitly set.
    targetPhLow  = prefs.getFloat("tgt_ph_lo", targetPhLow);
    targetPhHigh = prefs.getFloat("tgt_ph_hi", targetPhHigh);
    prefs.end();
}

void saveChemistryTargets(float alk, float ca, float mg) {
    targetAlk = alk;
    targetCa = ca;
    targetMg = mg;
    prefs.begin("doser-settings", false);
    prefs.putFloat("tgt_alk", alk);
    prefs.putFloat("tgt_ca", ca);
    prefs.putFloat("tgt_mg", mg);
    prefs.end();
    Serial.printf("CHEMISTRY TARGETS SAVED: Alk=%.2f Ca=%.1f Mg=%.1f\n", alk, ca, mg);
    logger.printf("CHEMISTRY TARGETS SAVED: Alk=%.2f Ca=%.1f Mg=%.1f\n", alk, ca, mg);
}

// Added 2026-08-05: deliberately a separate function rather than adding
// parameters to saveChemistryTargets() above -- that function's existing
// 3-argument signature is called from WebRoutes.cpp (not open this
// session), and changing it blind would break that call site. Once a
// dashboard field for pH target range exists, its handler should call
// this alongside (or instead of, once merged) saveChemistryTargets().
void saveChemistryTargetPhRange(float lo, float hi) {
    targetPhLow = lo;
    targetPhHigh = hi;
    prefs.begin("doser-settings", false);
    prefs.putFloat("tgt_ph_lo", lo);
    prefs.putFloat("tgt_ph_hi", hi);
    prefs.end();
    Serial.printf("PH TARGET RANGE SAVED: %.2f - %.2f\n", lo, hi);
    logger.printf("PH TARGET RANGE SAVED: %.2f - %.2f\n", lo, hi);
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

    // Push saved NVS calibration values into the live Doser object every boot.
    // Without this, /api/status may show the saved values but dose timing can
    // still use constructor/default calibration until a new save happens.
    for (int i = 0; i < 4; ++i) {
        doser.setCalibration(i, pumpFlowRates[i]);
    }
}

void saveFlowRate(int idx, float flowMlPerMin) {
    if (idx < 0 || idx > 3) return;
    if (!isfinite(flowMlPerMin) || flowMlPerMin <= 0.0f) return;

    pumpFlowRates[idx] = flowMlPerMin;
    prefs.begin("doser-settings", false);
    prefs.putFloat(flowPrefKeyForIndex(idx), flowMlPerMin);
    prefs.end();

    // Keep the running Doser object in sync immediately; otherwise the saved
    // value would not affect live-dose timing until a reboot.
    doser.setCalibration(idx, flowMlPerMin);
}

// ============================================================================
// ONE-TIME KALK LIMIT MIGRATION
// ----------------------------------------------------------------------------
// Production migration behavior for Eric:
//   * Runs only on reefDoser2.
//   * Runs only once for migration version 1 on reefDoser2's own NVS.
//   * Changes the known legacy ~35000 mL/day value to exactly 3 U.S. gallons/day.
//   * Never overwrites a different value previously saved from the dashboard.
//
// Each ESP32 has separate NVS, so using the same key/version tested on reefDoser3
// is safe: reefDoser2 has its own independent kalk_mig_v flag.
// ============================================================================
static constexpr uint32_t KALK_LIMIT_MIGRATION_VERSION = 1;
static constexpr float KALK_LIMIT_MIGRATION_OLD_ML_DAY = 35000.0f;
static constexpr float KALK_LIMIT_MIGRATION_TARGET_ML_DAY = 11356.23f; // Exactly 3.0 US gallons/day
static constexpr float KALK_LIMIT_MIGRATION_MATCH_TOLERANCE_ML = 1.0f;

void runOneTimeKalkLimitMigration() {
    if (deviceID != "reefDoser2") {
        return;
    }

    prefs.begin("doser-settings", false);

    const uint32_t savedVersion = prefs.getUInt("kalk_mig_v", 0);
    if (savedVersion >= KALK_LIMIT_MIGRATION_VERSION) {
        prefs.end();
        Serial.printf("KALK LIMIT MIGRATION: already applied (version=%lu); dashboard values preserved.\n",
                      (unsigned long)savedVersion);
        logger.printf("KALK LIMIT MIGRATION: already applied (version=%lu); dashboard values preserved.\n",
                      (unsigned long)savedVersion);
        return;
    }

    struct MigrationKey {
        const char* key;
        const char* label;
    };

    const MigrationKey keys[] = {
        {"base_kalk",   "AI baseline"},
        {"ai_max_kalk", "AI chemistry safety"},
        {"saf_p1_day",  "P1 daily safety"}
    };

    Serial.println("========== ONE-TIME KALK LIMIT MIGRATION ==========");
    logger.println("========== ONE-TIME KALK LIMIT MIGRATION ==========");
    Serial.printf("Device=%s version %lu -> %lu target=%.2f ml/day\n",
                  deviceID.c_str(),
                  (unsigned long)savedVersion,
                  (unsigned long)KALK_LIMIT_MIGRATION_VERSION,
                  KALK_LIMIT_MIGRATION_TARGET_ML_DAY);
    logger.printf("Device=%s version %lu -> %lu target=%.2f ml/day\n",
                  deviceID.c_str(),
                  (unsigned long)savedVersion,
                  (unsigned long)KALK_LIMIT_MIGRATION_VERSION,
                  KALK_LIMIT_MIGRATION_TARGET_ML_DAY);

    for (const MigrationKey& item : keys) {
        const bool exists = prefs.isKey(item.key);
        const float oldValue = exists ? prefs.getFloat(item.key, NAN) : NAN;
        const bool isLegacyValue = exists && isfinite(oldValue) &&
            fabsf(oldValue - KALK_LIMIT_MIGRATION_OLD_ML_DAY) <=
                KALK_LIMIT_MIGRATION_MATCH_TOLERANCE_ML;

        if (!exists || isLegacyValue) {
            prefs.putFloat(item.key, KALK_LIMIT_MIGRATION_TARGET_ML_DAY);
            Serial.printf("MIGRATED %-20s key=%s old=%s new=%.2f\n",
                          item.label,
                          item.key,
                          exists ? String(oldValue, 2).c_str() : "MISSING",
                          KALK_LIMIT_MIGRATION_TARGET_ML_DAY);
            logger.printf("MIGRATED %-20s key=%s old=%s new=%.2f\n",
                          item.label,
                          item.key,
                          exists ? String(oldValue, 2).c_str() : "MISSING",
                          KALK_LIMIT_MIGRATION_TARGET_ML_DAY);
        } else {
            Serial.printf("PRESERVED %-19s key=%s value=%.2f (dashboard/custom value)\n",
                          item.label, item.key, oldValue);
            logger.printf("PRESERVED %-19s key=%s value=%.2f (dashboard/custom value)\n",
                          item.label, item.key, oldValue);
        }
    }

    // Write the flag only after all migration decisions/writes complete.
    prefs.putUInt("kalk_mig_v", KALK_LIMIT_MIGRATION_VERSION);
    prefs.end();

    Serial.println("KALK LIMIT MIGRATION COMPLETE: version flag saved; it will not run again.");
    Serial.println("===================================================");
    logger.println("KALK LIMIT MIGRATION COMPLETE: version flag saved; it will not run again.");
    logger.println("===================================================");
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
    bool hasBaseKalk = prefs.isKey("base_kalk");
    baselineKalkMlDay  = hasBaseKalk ? prefs.getFloat("base_kalk", baselineKalkMlDay) : baselineKalkMlDay;
    baselineCacl2MlDay = prefs.getFloat("base_cacl2", baselineCacl2MlDay);
    baselineNaohMlDay  = prefs.getFloat("base_naoh", baselineNaohMlDay);
    baselineMgMlDay    = prefs.getFloat("base_mg", baselineMgMlDay);
    baselineCoralLoad  = prefs.getString("base_load", baselineCoralLoad);
    notificationLevel  = prefs.getString("notif_level", notificationLevel);
    if (!isValidNotificationLevel(notificationLevel)) notificationLevel = "warning";
    prefs.end();

    // Mode 7 must not come up with kalk/day = 0. Saved positive baseline wins;
    // missing or zero baseline gets a tank-size-scaled default.
    if ((dosingMode == 7 || dosingMode == 8) && (!hasBaseKalk || !isfinite(baselineKalkMlDay) || baselineKalkMlDay <= 0.0f)) {
        baselineKalkMlDay = defaultMode7KalkBaselineMlDay();
        Serial.printf("MODE7/8 KALK BASELINE DEFAULT: using %.2f ml/day because saved base_kalk is missing/zero\n", baselineKalkMlDay);
        logger.printf("MODE7/8 KALK BASELINE DEFAULT: using %.2f ml/day because saved base_kalk is missing/zero\n", baselineKalkMlDay);
    }

    loadPumpSafeties();
    loadMode7DayNightSplit();
    loadAiChemistrySafeties();

    // v1 -> v2: tank volume/coral-load now live directly on ai.tank (also
    // refreshed every cycle in runAiRecalculation()). baselineKalkMlDay/etc.
    // are no longer pushed into the engine -- see comment near
    // runAiRecalculation() for why.
    ai.tank.tankVolumeLiters = TANK_VOLUME_L;
    ai.tank.coralLoad = coralLoadFromBaselineString(baselineCoralLoad);

    if (!isValidSystemMode(systemMode)) systemMode = 1;
    if (!isValidDosingMode(dosingMode)) dosingMode = 1;
    apexIp = savedIp;
    if (savedIp.length() > 0) {
        apex.setIpAddr(savedIp);
    }

    loadFlowRates();
    loadManualTestLocally();
    loadSetupWizardCompleted();
    loadChemistryTargets();
}

bool publishTankVolumeToFirebase(const char* source) {
    if (apexTlsReservedOrCoolingDown()) return false;
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

bool mirrorStatusToFirebase() {
    if (apexTlsReservedOrCoolingDown()) return false;
    if (WiFi.status() != WL_CONNECTED) {
        return false;
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
        return false;
    }

    FirebaseJson stateJson;
    stateJson.set("online", true);
    stateJson.set("lastSeenUnix", (uint32_t)time(nullptr));
    stateJson.set("lastSeenDeviceSec", (uint32_t)(millis() / 1000UL));
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
    stateJson.set("notificationLevel", notificationLevel);
    stateJson.set("alertState/active", currentAlertActive);
    stateJson.set("alertState/level", currentAlertLevel);
    stateJson.set("alertState/code", currentAlertCode);
    stateJson.set("alertState/message", currentAlertMessage);
    stateJson.set("systemMode", systemMode);
    stateJson.set("dosingMode", dosingMode);
    // Preserve legacy flow keys and add chemical aliases so Firebase reflects local status better.
    // Physical pump keys are always published; chemical keys below are corrected for Mode 7.
    stateJson.set("flowMlPerMin/p1", pumpFlowRates[0]);
    stateJson.set("flowMlPerMin/p2", pumpFlowRates[1]);
    stateJson.set("flowMlPerMin/p3", pumpFlowRates[2]);
    stateJson.set("flowMlPerMin/p4", pumpFlowRates[3]);
    stateJson.set("flowMlPerMin/kalk", pumpFlowRates[0]);
    stateJson.set("flowMlPerMin/afr", pumpFlowRates[1]);
    stateJson.set("flowMlPerMin/tbd", pumpFlowRates[3]);
    stateJson.set("flowMlPerMin/ca", pumpFlowRates[1]);
    stateJson.set("flowMlPerMin/cacl2", pumpFlowRates[1]);
    stateJson.set("flowMlPerMin/naoh", pumpFlowRates[2]);
    if (dosingMode == 7) {
        stateJson.set("flowMlPerMin/alk", pumpFlowRates[3]);
        stateJson.set("flowMlPerMin/mg", 0.0f);
    } else if (dosingMode == 8) {
        stateJson.set("flowMlPerMin/alk", 0.0f);
        stateJson.set("flowMlPerMin/mg", 0.0f);
    } else {
        stateJson.set("flowMlPerMin/alk", pumpFlowRates[0]);
        stateJson.set("flowMlPerMin/mg", pumpFlowRates[2]);
    }

    String path = "/devices/" + deviceID + "/state";
    if (Firebase.updateNode(writeFbdo, path.c_str(), stateJson)) {
        static unsigned long lastOkLog = 0;
        if (millis() - lastOkLog > 60000UL) {
            lastOkLog = millis();
            Serial.println("Firebase mirror OK: device state/water parameters pushed to RTDB.");
            logger.println("Firebase mirror OK: device state/water parameters pushed to RTDB.");
        }
        lastFirebaseMirrorMs = millis();
        return true;
    } else {
        Serial.printf("Firebase mirror FAILED: %s\n", writeFbdo.errorReason().c_str());
        logger.printf("Firebase mirror FAILED: %s\n", writeFbdo.errorReason().c_str());
        return false;
    }
}

// handleGetStatus() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostVolume() moved to lib/WebRoutes/WebRoutes.cpp

// handleGetMode() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostMode() moved to lib/WebRoutes/WebRoutes.cpp

// handleGetDosingMode() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostDosingMode() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostApexLocal() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostCalibration() moved to lib/WebRoutes/WebRoutes.cpp


// handlePostCalibrationRun() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostLightConfig() moved to lib/WebRoutes/WebRoutes.cpp

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

// handlePostLiveDose() moved to lib/WebRoutes/WebRoutes.cpp


// handleGetChemicalLevels() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostChemicalLevels() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostEmergencyStop() moved to lib/WebRoutes/WebRoutes.cpp


unsigned long alertRepeatMsForLevel(const String& level) {
    if (level == "severe") return ALERT_SEVERE_REPEAT_MS;
    if (level == "warning") return ALERT_WARNING_REPEAT_MS;
    if (level == "info") return ALERT_INFO_REPEAT_MS;
    return ALERT_WARNING_REPEAT_MS;
}

bool publishAlertState(const String& level, const String& code, const String& message, bool active, const char* source, bool force) {
    if (WiFi.status() != WL_CONNECTED || !firebaseStarted || !Firebase.ready()) return false;

    unsigned long nowMs = millis();
    bool changed = force ||
                   (active != currentAlertActive) ||
                   (level != currentAlertLevel) ||
                   (code != currentAlertCode) ||
                   (message != currentAlertMessage);
    bool cooldownDue = active && lastAlertWriteMs > 0 &&
                       (nowMs - lastAlertWriteMs >= alertRepeatMsForLevel(level));

    if (!changed && !cooldownDue) return false;

    FirebaseJson json;
    json.set("active", active);
    json.set("level", level);
    json.set("code", code);
    json.set("message", message);
    json.set("source", source ? source : "firmware");
    json.set("deviceId", deviceID);
    json.set("updatedAtDeviceSec", (uint32_t)(millis() / 1000UL));
    json.set("updatedAtUnix", (uint32_t)time(nullptr));

    String path = "/devices/" + deviceID + "/alertState";
    if (Firebase.updateNode(writeFbdo, path.c_str(), json)) {
        currentAlertActive = active;
        currentAlertLevel = level;
        currentAlertCode = code;
        currentAlertMessage = message;
        lastAlertWriteMs = nowMs;

        Serial.printf("ALERT STATE [%s]: active=%s level=%s code=%s msg=%s\n",
                      source ? source : "firmware", active ? "true" : "false",
                      level.c_str(), code.c_str(), message.c_str());
        logger.printf("ALERT STATE [%s]: active=%s level=%s code=%s msg=%s\n",
                      source ? source : "firmware", active ? "true" : "false",
                      level.c_str(), code.c_str(), message.c_str());
        return true;
    }

    Serial.printf("ALERT STATE publish failed: %s\n", writeFbdo.errorReason().c_str());
    logger.printf("ALERT STATE publish failed: %s\n", writeFbdo.errorReason().c_str());
    return false;
}

void evaluateAlertState(const char* source, bool force) {
    unsigned long nowMs = millis();
    if (!force && lastAlertEvalMs != 0 && (nowMs - lastAlertEvalMs) < ALERT_EVAL_EVERY_MS) return;
    lastAlertEvalMs = nowMs;

    String level = "normal";
    String code = "OK";
    String message = "All monitored values normal";
    bool active = false;

    // Highest-priority first. Keep this short to avoid notification noise.
    if (emergencyStop) {
        active = true; level = "severe"; code = "EMERGENCY_STOP";
        message = emergencyStopReason.length() > 0
                    ? emergencyStopReason
                    : "Emergency stop is active.";
    } else if (currentAlk > 0.0f && currentAlk < 6.50f) {
        active = true; level = "severe"; code = "ALK_CRITICAL_LOW";
        message = "Alk is critically low: " + String(currentAlk, 2) + " dKH";
    } else if (currentAlk > 10.00f) {
        active = true; level = "severe"; code = "ALK_CRITICAL_HIGH";
        message = "Alk is critically high: " + String(currentAlk, 2) + " dKH";
    } else if (currentPh > 0.0f && currentPh < 7.75f) {
        active = true; level = "severe"; code = "PH_CRITICAL_LOW";
        message = "pH is critically low: " + String(currentPh, 2);
    } else if (currentPh > 8.65f) {
        active = true; level = "severe"; code = "PH_CRITICAL_HIGH";
        message = "pH is critically high: " + String(currentPh, 2);
    } else if (currentTempF > 0.0f && currentTempF < 75.0f) {
        active = true; level = "severe"; code = "TEMP_CRITICAL_LOW";
        message = "Temperature is critically low: " + String(currentTempF, 1) + " F";
    } else if (currentTempF > 82.5f) {
        active = true; level = "severe"; code = "TEMP_CRITICAL_HIGH";
        message = "Temperature is critically high: " + String(currentTempF, 1) + " F";
    } else {
        for (int i = 0; i < 4; ++i) {
            if (chemicalCapacityGal[i] > 0.0f && chemicalRemainingMl[i] <= (0.5f * ML_PER_GALLON)) {
                active = true; level = "severe"; code = "CHEM_P" + String(i + 1) + "_CRITICAL_LOW";
                message = "Pump " + String(i + 1) + " chemical is critically low: " + String(chemicalRemainingMl[i] / ML_PER_GALLON, 2) + " gal left";
                break;
            }
        }
    }

    if (!active && currentAlk > 0.0f && currentAlk < 7.00f) {
        active = true; level = "warning"; code = "ALK_LOW";
        message = "Alk is low: " + String(currentAlk, 2) + " dKH";
    } else if (currentAlk > 9.30f) {
        active = true; level = "warning"; code = "ALK_HIGH";
        message = "Alk is high: " + String(currentAlk, 2) + " dKH";
    } else if (currentPh > 0.0f && currentPh < 7.90f) {
        active = true; level = "warning"; code = "PH_LOW";
        message = "pH is low: " + String(currentPh, 2);
    } else if (currentPh > 8.50f) {
        active = true; level = "warning"; code = "PH_HIGH";
        message = "pH is high: " + String(currentPh, 2);
    } else if (currentTempF > 0.0f && currentTempF < 76.0f) {
        active = true; level = "warning"; code = "TEMP_LOW";
        message = "Temperature is low: " + String(currentTempF, 1) + " F";
    } else if (currentTempF > 81.5f) {
        active = true; level = "warning"; code = "TEMP_HIGH";
        message = "Temperature is high: " + String(currentTempF, 1) + " F";
    } else {
        for (int i = 0; i < 4; ++i) {
            if (chemicalCapacityGal[i] > 0.0f && chemicalRemainingMl[i] <= ML_PER_GALLON) {
                active = true; level = "warning"; code = "CHEM_P" + String(i + 1) + "_LOW";
                message = "Pump " + String(i + 1) + " chemical is low: " + String(chemicalRemainingMl[i] / ML_PER_GALLON, 2) + " gal left";
                break;
            }
        }
    }

    publishAlertState(level, code, message, active, source, force);
}

// handleGetNotificationSettings() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostNotificationSettings() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostResetWifi() moved to lib/WebRoutes/WebRoutes.cpp


// handlePostAiBaseline() moved to lib/WebRoutes/WebRoutes.cpp


// handlePostMode7DayNightSplit() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostAiChemistrySafeties() moved to lib/WebRoutes/WebRoutes.cpp

// handlePostDosingSafeties() moved to lib/WebRoutes/WebRoutes.cpp

// Accept one learner event for each genuinely new chemistry measurement.
// Apex/Trident uses a timestamp when available or an Alk/Ca/Mg fingerprint
// when it is not. Manual submissions use forceNew=true because each press of
// Save Test represents a new physical test, even when the rounded values match
// the previous result. All sources feed the same fast/mid/long-term learners.
bool acceptNewChemistryMeasurement(const char* source, const String& measurementId = "", bool forceNew = false) {
    bool isNew = false;

    // Added 2026-09-01, TEMPORARY diagnostic -- remove once the confirmed
    // real gap (13 of 24 real changes in a 7-day sample not tagged "new")
    // is understood. Prints the ACTUAL comparison inputs on EVERY call --
    // not just the outcome -- so it's possible to see directly whether
    // lastAcceptedTridentAlk/Ca/Mg are being updated (consumed) on a call
    // that never produces its own "AI chemistry" summary line, which
    // would explain a real change appearing to be "missed" when it was
    // actually correctly caught one call earlier than visible from the
    // summary print alone.
    {
        const char* pathTaken = forceNew ? "forceNew"
                               : (measurementId.length() > 0) ? "measurementId"
                               : (!haveAcceptedTridentFingerprint) ? "firstEverFingerprint"
                               : "fingerprintCompare";
        Serial.printf("ACCEPT-CHECK [%s] path=%s current(Alk=%.4f,Ca=%.2f,Mg=%.2f) "
                      "lastAccepted(Alk=%.4f,Ca=%.2f,Mg=%.2f) diff(Alk=%.4f,Ca=%.2f,Mg=%.2f)\n",
                      source ? source : "?", pathTaken,
                      currentAlk, currentCa, currentMg,
                      lastAcceptedTridentAlk, lastAcceptedTridentCa, lastAcceptedTridentMg,
                      fabsf(currentAlk - lastAcceptedTridentAlk),
                      fabsf(currentCa  - lastAcceptedTridentCa),
                      fabsf(currentMg  - lastAcceptedTridentMg));
        logger.printf("ACCEPT-CHECK [%s] path=%s current(Alk=%.4f,Ca=%.2f,Mg=%.2f) "
                      "lastAccepted(Alk=%.4f,Ca=%.2f,Mg=%.2f) diff(Alk=%.4f,Ca=%.2f,Mg=%.2f)\n",
                      source ? source : "?", pathTaken,
                      currentAlk, currentCa, currentMg,
                      lastAcceptedTridentAlk, lastAcceptedTridentCa, lastAcceptedTridentMg,
                      fabsf(currentAlk - lastAcceptedTridentAlk),
                      fabsf(currentCa  - lastAcceptedTridentCa),
                      fabsf(currentMg  - lastAcceptedTridentMg));
    }

    if (forceNew) {
        isNew = true;
    } else if (measurementId.length() > 0) {
        isNew = lastAcceptedEmulatorTestTime.length() == 0 ||
                measurementId != lastAcceptedEmulatorTestTime;
        if (isNew) lastAcceptedEmulatorTestTime = measurementId;
    } else if (!haveAcceptedTridentFingerprint) {
        isNew = true;
    } else {
        // Match the precision normally reported by Trident/Apex. A change in any
        // one of the three Trident analytes identifies a new completed result.
        isNew = fabsf(currentAlk - lastAcceptedTridentAlk) >= 0.005f ||
                fabsf(currentCa  - lastAcceptedTridentCa)  >= 0.5f   ||
                fabsf(currentMg  - lastAcceptedTridentMg)  >= 0.5f;
    }

    if (isNew) {
        haveAcceptedTridentFingerprint = true;
        lastAcceptedTridentAlk = currentAlk;
        lastAcceptedTridentCa = currentCa;
        lastAcceptedTridentMg = currentMg;
        newChemistrySamplePendingForDailyStats = true;
        Serial.printf("CHEMISTRY NEW MEASUREMENT ACCEPTED [%s]: Alk=%.2f Ca=%.1f Mg=%.1f%s%s\n",
                      source ? source : "Unknown", currentAlk, currentCa, currentMg,
                      measurementId.length() ? " measurementId=" : "",
                      measurementId.length() ? measurementId.c_str() : "");
        logger.printf("CHEMISTRY NEW MEASUREMENT ACCEPTED [%s]: Alk=%.2f Ca=%.1f Mg=%.1f%s%s\n",
                      source ? source : "Unknown", currentAlk, currentCa, currentMg,
                      measurementId.length() ? " measurementId=" : "",
                      measurementId.length() ? measurementId.c_str() : "");
    } else {
        Serial.printf("CHEMISTRY REPEAT POLL [%s]: learner counters held Alk=%.2f Ca=%.1f Mg=%.1f\n",
                      source ? source : "Unknown", currentAlk, currentCa, currentMg);
        logger.printf("CHEMISTRY REPEAT POLL [%s]: learner counters held Alk=%.2f Ca=%.1f Mg=%.1f\n",
                      source ? source : "Unknown", currentAlk, currentCa, currentMg);
    }

    // v1 -> v2: this used to tell the fastAlk learner "count this as a
    // genuinely new sample" (ai.setFastAlkNewMeasurement), which is gone.
    // `isNew` is still returned/used by callers for daily-stats dedup.
    // TODO(migration): runAiRecalculation()'s ai.ingestMeasurement() calls
    // are NOT currently gated on this dedup flag -- every AI recalculation
    // cycle feeds the Kalman filter whatever currentAlk/currentCa/currentMg
    // are at that moment, even if it's a repeated poll of the same Trident
    // result. Feeding an unchanged value repeatedly is not dangerous (the
    // filter just sees zero innovation), but it will shrink that
    // parameter's covariance (maturity) faster than genuinely-new-data
    // would justify. Wiring `isNew` through to gate ingestMeasurement() is
    // a reasonable follow-up, not done here to avoid re-threading this flag
    // through every calculateAiFromBestChemistry() call site blind.
    return isNew;
}

// Compatibility wrapper for existing Apex call sites.
bool acceptNewTridentMeasurement(const char* source, const String& testTime = "") {
    return acceptNewChemistryMeasurement(source, testTime, false);
}

// 3. The "Trigger" logic (run when Apex data arrives)
// Fixed 2026-07-24: isNewMeasurement now actually reaches
// calculateAiFromBestChemistry() -- previously acceptNewTridentMeasurement()'s
// return value was computed correctly (the "CHEMISTRY REPEAT POLL" vs
// "NEW MEASUREMENT ACCEPTED" log messages were already right) but then
// discarded at both call sites below, so every Apex poll re-ingested
// whatever the current reading was regardless, even a repeat of the same
// unchanged Trident result. See runAiRecalculation()'s comment for the
// full explanation of why that matters.
void onNewDataArrived(float rawAlk, bool isNewMeasurement) {
    alkHistory[alkIdx] = rawAlk;
    alkIdx = (alkIdx + 1) % 5;

    // Mode 7 bug fix:
    // Use the exact same AI calculation path that already works for Hourly/live chemistry.
    // The old Apex-only calculation could publish a stale/wrong split like:
    // kalk=0, alk=1000, cacl2=640 even while Mode 7 buckets were working locally.
    if (currentAlk <= 0.0f || currentAlk > 20.0f ||
        currentCa  < 250.0f || currentCa  > 700.0f ||
        currentMg  < 800.0f || currentMg  > 1800.0f ||
        currentPh  < 6.50f  || currentPh  > 9.00f) {
        Serial.printf("Apex AI skipped: invalid chemistry Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f\n",
                      currentAlk, currentCa, currentMg, currentPh);
        logger.printf("Apex AI skipped: invalid chemistry Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f\n",
                      currentAlk, currentCa, currentMg, currentPh);
        return;
    }

    calculateAiFromBestChemistry("Apex", isNewMeasurement);
    addCurrentAiPlanToBuckets("Apex", false);
    publishAiPlanIfNeeded("Apex", true);
}


void apexEmulatorTask(void* parameter) {
    (void)parameter;

    // Give setup/Wi-Fi/Firebase startup time to settle before the first HTTPS call.
    vTaskDelay(pdMS_TO_TICKS(APEX_EMULATOR_POLL_MS));

    for (;;) {
        bool ok = false;
        float alk = 0.0f;
        float ca = 0.0f;
        float mg = 0.0f;
        float ph = 0.0f;
        String source = "apex-log-emulator";
        String logTime = "";
        String error = "";

        if (!useApexLogEmulatorForThisDevice()) {
            error = "emulator disabled for this device";
        } else if (WiFi.status() != WL_CONNECTED) {
            error = "wifi not connected";
        } else {
            // Reserve TLS before starting. loopTask sees this flag and pauses
            // Firebase stream/writes and Google Drive logger maintenance.
            apexTlsReservation = true;
            vTaskDelay(pdMS_TO_TICKS(3000));

            ok = apexEmu.pollNow();
            const ApexEmuChemistry& c = apexEmu.chemistry();

            if (ok) {
                alk = c.alk;
                ca = c.ca;
                mg = c.mg;
                ph = c.ph;
                source = c.source;
                logTime = c.logTime;
            } else {
                error = c.error;
            }

            // BearSSL cleanup can continue briefly after HTTPClient returns.
            apexTlsCooldownUntilMs = millis() + 15000UL;
            apexTlsReservation = false;
        }

        portENTER_CRITICAL(&apexEmulatorResultMux);
        apexEmulatorResultOk = ok;
        apexEmulatorResultAlk = alk;
        apexEmulatorResultCa = ca;
        apexEmulatorResultMg = mg;
        apexEmulatorResultPh = ph;

        snprintf(apexEmulatorResultSource,
                 sizeof(apexEmulatorResultSource),
                 "%s",
                 source.c_str());
        snprintf(apexEmulatorResultLogTime,
                 sizeof(apexEmulatorResultLogTime),
                 "%s",
                 logTime.c_str());
        snprintf(apexEmulatorResultError,
                 sizeof(apexEmulatorResultError),
                 "%s",
                 error.c_str());

        apexEmulatorResultPending = true;
        portEXIT_CRITICAL(&apexEmulatorResultMux);

        // The emulator is a test data source. Poll every five minutes without
        // ever blocking loopTask, pump supervision, logger service, or OTA.
        vTaskDelay(pdMS_TO_TICKS(300000UL));
    }
}

bool serviceApexEmulatorResult() {
    if (!useApexLogEmulatorForThisDevice()) return false;

    bool pending = false;
    bool ok = false;
    float alk = 0.0f;
    float ca = 0.0f;
    float mg = 0.0f;
    float ph = 0.0f;
    char source[96] = {0};
    char logTime[40] = {0};
    char error[160] = {0};

    portENTER_CRITICAL(&apexEmulatorResultMux);
    pending = apexEmulatorResultPending;

    if (pending) {
        ok = apexEmulatorResultOk;
        alk = apexEmulatorResultAlk;
        ca = apexEmulatorResultCa;
        mg = apexEmulatorResultMg;
        ph = apexEmulatorResultPh;

        snprintf(source, sizeof(source), "%s", apexEmulatorResultSource);
        snprintf(logTime, sizeof(logTime), "%s", apexEmulatorResultLogTime);
        snprintf(error, sizeof(error), "%s", apexEmulatorResultError);

        apexEmulatorResultPending = false;
    }
    portEXIT_CRITICAL(&apexEmulatorResultMux);

    if (!pending) return false;

    if (!ok) {
        Serial.printf("APEX EMU ASYNC FAILED: %s\n",
                      error[0] ? error : "unknown error");
        logger.printf("APEX EMU ASYNC FAILED: %s\n",
                      error[0] ? error : "unknown error");
        return false;
    }

    // Require all four chemistry values before changing the AI plan.
    if (!isfinite(alk) || !isfinite(ca) || !isfinite(mg) || !isfinite(ph) ||
        alk <= 0.0f || alk > 20.0f ||
        ca < 250.0f || ca > 700.0f ||
        mg < 800.0f || mg > 1800.0f ||
        ph < 6.50f || ph > 9.00f) {
        Serial.printf("APEX EMU ASYNC REJECTED: Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f\n",
                      alk, ca, mg, ph);
        logger.printf("APEX EMU ASYNC REJECTED: Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f\n",
                      alk, ca, mg, ph);
        return false;
    }

    currentAlk = alk;
    currentCa = ca;
    currentMg = mg;
    currentPh = ph;

    Serial.printf("APEX EMU ASYNC OK [%s]: Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f logTime=%s\n",
                  source, currentAlk, currentCa, currentMg, currentPh, logTime);
    logger.printf("APEX EMU ASYNC OK [%s]: Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f logTime=%s\n",
                  source, currentAlk, currentCa, currentMg, currentPh, logTime);

    bool isNewTridentResult = acceptNewTridentMeasurement("ApexEmulator", String(logTime));
    onNewDataArrived(currentAlk, isNewTridentResult);
    return true;
}

bool syncApexLogEmulatorTruths() {
    // Compatibility wrapper: HTTPS is no longer performed here.
    return serviceApexEmulatorResult();
}

void syncAllTruths() {
    if (useApexLogEmulatorForThisDevice()) {
        syncApexLogEmulatorTruths();

        if (millis() - lastFirebaseMirrorMs >= FIREBASE_MIRROR_INTERVAL_MS) {
            mirrorStatusToFirebase();
        }
        return;
    }

    if (!apexEnabled || apexIp.length() < 7) {
        Serial.println("Apex skipped: disabled or missing IP");
        logger.println("Apex skipped: disabled or missing IP");
        return;
    }

    String response = apex.getState();
    if (response.length() > 0) {
        float nextTempF = apex.getTempF();
        float nextPh    = apex.getPh();
        float nextAlk   = apex.getAlk();
        float nextCa    = apex.getCa();
        float nextMg    = apex.getMg();
        float nextCond  = apex.getCond();

        // Reject bad Apex parser/fallback values before they poison saved state or Firebase.
        // This blocks the earlier 650.00 dKH style failure.
        if (nextAlk <= 0.0f || nextAlk > 20.0f ||
            nextCa  < 250.0f || nextCa  > 700.0f ||
            nextMg  < 800.0f || nextMg  > 1800.0f ||
            nextPh  < 6.50f  || nextPh  > 9.00f) {
            Serial.printf("Apex rejected: invalid chemistry Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f\n",
                          nextAlk, nextCa, nextMg, nextPh);
            logger.printf("Apex rejected: invalid chemistry Alk=%.2f Ca=%.1f Mg=%.1f pH=%.2f\n",
                          nextAlk, nextCa, nextMg, nextPh);
            return;
        }

        currentTempF = nextTempF;
        currentPh    = nextPh;
        currentAlk   = nextAlk;
        currentCa    = nextCa;
        currentMg    = nextMg;
        currentCond  = nextCond;

        // Distinguish a genuinely new Trident test from a repeated Apex poll.
        // The AI plan still recalculates every poll (settings/targets may
        // have changed), but the Kalman filter is only fed a new
        // measurement when Alk/Ca/Mg identify an actually-changed Trident
        // result. Fixed 2026-07-24: this comment described the intended
        // behavior correctly, but the code didn't actually do it --
        // acceptNewTridentMeasurement()'s return value was discarded here,
        // so every poll re-ingested the current reading regardless.
        bool isNewTridentResult = acceptNewTridentMeasurement("Apex");

        // Recalculate AI after all Apex values are current, then publish the plan to Firebase.
        onNewDataArrived(currentAlk, isNewTridentResult);

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
        mirrorStatusToFirebase();
    }

    // Low-cost alerts: evaluate locally, write only on alert state change/cooldown.
    evaluateAlertState("Apex", false);
    
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
    Serial.printf("FW VERSION PUSHED: %s to %s\n", FW_VERSION.c_str(), fwPath.c_str());
    logger.printf("FW VERSION PUSHED: %s to %s\n", FW_VERSION.c_str(), fwPath.c_str());
    WebSerial.printf("FW VERSION PUSHED: %s\n", FW_VERSION.c_str());
}

void connectToFirebase() {
    config.api_key = FIREBASE_API_KEY;
    config.database_url = FIREBASE_DB_URL;
    config.token_status_callback = tokenStatusCallback;

    // Use the stable Firebase Auth account generated for this device.
    // Do not call Firebase.signUp() here; empty credentials create anonymous users.
    auth.user.email = BUILD_FIREBASE_EMAIL;
    auth.user.password = BUILD_FIREBASE_PASSWORD;

    if (auth.user.email.length() == 0 || auth.user.password.length() < 8) {
        Serial.printf(
            "FIREBASE AUTH BLOCKED for %s: permanent credentials are missing or invalid.\n",
            deviceID.c_str()
        );
        logger.printf(
            "FIREBASE AUTH BLOCKED for %s: permanent credentials are missing or invalid.\n",
            deviceID.c_str()
        );
        firebaseStarted = false;
        return;
    }

    Serial.printf(
        "Firebase permanent auth starting for %s as %s\n",
        deviceID.c_str(),
        auth.user.email.c_str()
    );
    logger.printf(
        "Firebase permanent auth starting for %s as %s\n",
        deviceID.c_str(),
        auth.user.email.c_str()
    );

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // Publish current notification setting once after Firebase starts.
    Firebase.setString(
        writeFbdo,
        ("/devices/" + deviceID + "/settings/notificationLevel").c_str(),
        notificationLevel
    );

    // Try now, then loop() will retry once Firebase.ready() is true.
    publishFirmwareVersionIfReady(true);

    String path = "/devices/" + deviceID + "/commands";
    if (!Firebase.beginStream(streamFbdo, path.c_str())) {
        Serial.printf("Stream error: %s\n", streamFbdo.errorReason().c_str());
        logger.printf("Stream error: %s\n", streamFbdo.errorReason().c_str());
    }

    Firebase.setStreamCallback(
        streamFbdo,
        streamCallback,
        streamTimeoutCallback
    );
}

// handleRoot() moved to lib/WebRoutes/WebRoutes.cpp

uint32_t localDayKeyWithOffsetDays(int offsetDays) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) return 0;
    time_t t = mktime(&timeinfo) + (time_t)offsetDays * 86400;
    localtime_r(&t, &timeinfo);
    return (uint32_t)(timeinfo.tm_year + 1900) * 10000UL +
           (uint32_t)(timeinfo.tm_mon + 1) * 100UL +
           (uint32_t)timeinfo.tm_mday;
}

// saveAlkDemandHistory() moved to lib/DemandLearning/DemandLearning.cpp

// loadAlkDemandHistory() moved to lib/DemandLearning/DemandLearning.cpp

// printAlkDemandRecommendation() moved to lib/DemandLearning/DemandLearning.cpp



// publishAlkDemandStatusToFirebase() moved to lib/DemandLearning/DemandLearning.cpp

// recordCompletedDayAndLearn() moved to lib/DemandLearning/DemandLearning.cpp


// saveCalciumDemandHistory() moved to lib/DemandLearning/DemandLearning.cpp

// loadCalciumDemandHistory() moved to lib/DemandLearning/DemandLearning.cpp

// printCalciumDemandRecommendation() moved to lib/DemandLearning/DemandLearning.cpp

// publishCalciumDemandStatusToFirebase() moved to lib/DemandLearning/DemandLearning.cpp

// recordCompletedCalciumDayAndLearn() moved to lib/DemandLearning/DemandLearning.cpp

void pushDailyReport() {
    if (dailyStats.count == 0) {
        Serial.println("Midnight report skipped: no daily samples yet.");
        logger.println("Midnight report skipped: no daily samples yet.");
        return;
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) return;

    // Fixed 2026-08-05: this function only ever runs in the 10-minute
    // window right after midnight (see its caller) -- meaning
    // getLocalTime() here always returns the NEW day that just started,
    // not the day whose totals (dailyStats/dailyDoseTotals) are actually
    // being reported. Every record got stamped with the date it was
    // WRITTEN (today) instead of the date it SUMMARIZES (yesterday).
    // Subtracting one day here corrects that -- the history record for
    // "yesterday" is written under yesterday's actual date.
    time_t nowEpoch = mktime(&timeinfo);
    time_t yesterdayEpoch = nowEpoch - 86400;
    struct tm reportDay;
    localtime_r(&yesterdayEpoch, &reportDay);

    char monthFolder[8]; // YYYY-MM
    char dayFolder[3];   // DD
    strftime(monthFolder, sizeof(monthFolder), "%Y-%m", &reportDay);
    strftime(dayFolder, sizeof(dayFolder), "%d", &reportDay);

    float count = (float)dailyStats.count;
    float avgPh   = dailyStats.phSum / count;
    float avgTemp = dailyStats.tempSum / count;
    float avgAlk  = dailyStats.alkSum / count;
    float avgCa   = (dailyStats.caSum  > 0.0f) ? (dailyStats.caSum  / count) : currentCa;
    float avgMg   = (dailyStats.mgSum  > 0.0f) ? (dailyStats.mgSum  / count) : currentMg;
    float avgPpt  = (dailyStats.pptSum > 0.0f) ? (dailyStats.pptSum / count) : currentPpt;
    float avgSg   = (dailyStats.sgSum  > 0.0f) ? (dailyStats.sgSum  / count) : currentSg;
    float totalDose = dailyDoseTotals[0] + dailyDoseTotals[1] + dailyDoseTotals[2] + dailyDoseTotals[3];

    // Removed 2026-08-02: recordCompletedDayAndLearn()/
    // recordCompletedCalciumDayAndLearn() computed a real result and wrote
    // it to a rolling LittleFS file every day, but nothing has read that
    // output since applyAiBaselineToEngine() (confirmed empty stub) was
    // the only consumer of it. Their corresponding dashboard cards were
    // already removed from Dashboard.h for the same reason. Stopped here
    // too rather than leaving them silently running -- this was genuine
    // wasted daily flash wear/CPU for a value nothing uses. The function
    // bodies themselves are left in lib/DemandLearning/DemandLearning.cpp
    // untouched (harmless unreachable code now) rather than deleted, same
    // conservative approach used elsewhere tonight.
    // recordCompletedDayAndLearn(avgAlk);
    // recordCompletedCalciumDayAndLearn(avgCa);

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

    // Write only chemical aliases for history. Do not also write p1/p2/p3/p4 here,
    // because the hosted index page displays chemical dosing history and older records
    // with both physical and chemical keys are easy to misread or double-count.
    for (int i = 0; i < 4; ++i) {
        const char* key = pumpKeyForPhysicalIndex(i);
        if (strcmp(key, "unused") != 0) {
            json.set(String("dosing/") + key, dailyDoseTotals[i]);
        }
    }

    // Fixed 2026-08-02: the loop above keys purely off pumpKeyForPhysicalIndex(),
    // which derives its label from the legacy `dosingMode` int (1-8) -- a
    // completely separate, often-stale concept from what a v2 customer has
    // actually declared via the free chemical-assignment wizard. For any
    // pump whose real declared chemical doesn't match that legacy table
    // (or where dosingMode itself is stale/default), this returns "unused"
    // and the `if` guard above SILENTLY DROPS that day's real dispensed
    // total from history entirely -- not mislabeled, just missing. Same
    // root cause class as the live-plan bug fixed in publishAiPlanIfNeeded()
    // (name/legacy-table matching can never cover an arbitrary customer
    // assignment), just biting the actually-dispensed-volume side instead
    // of the intended-plan side this time.
    //
    // Mirrors the already-correct plan/pump1..4 pattern a few lines below
    // in this same function (added earlier for the intended-dose side) --
    // NOT a double-count risk despite the comment above: Dashboard.h's
    // historyValue() helper does a FALLBACK lookup across candidate keys
    // (['p1','P1','pump1','0','kalk','afr','alk'], etc.), not a sum, so an
    // additional pump-indexed key sits alongside the legacy alias without
    // being added to it.
    json.set("dosing/pump1", dailyDoseTotals[0]);
    json.set("dosing/pump2", dailyDoseTotals[1]);
    json.set("dosing/pump3", dailyDoseTotals[2]);
    json.set("dosing/pump4", dailyDoseTotals[3]);

    // Daily copy of the latest plan, so the cloud UI can show what the controller intended.
    // Legacy named-field mirror -- best-effort (see syncLegacyPlanFromV2()),
    // kept during the Phase 1-3 transition for any existing cloud dashboard
    // still reading these paths.
    json.set("plan/kalk", currentPlan.kalk);
    json.set("plan/afr", currentPlan.afr);
    json.set("plan/alk", currentPlan.alk);
    json.set("plan/cacl2", currentPlan.cacl2);
    json.set("plan/naoh", currentPlan.naoh);
    json.set("plan/mg", currentPlan.mg);
    // §5 free chemical declaration: mode-agnostic mirror by physical pump
    // index, correct for any declared chemical set (not just the six
    // legacy names above). This is the one that should keep working once
    // a customer declares something that isn't Kalk/AFR/Alk/CaCl2/NaOH/Mg.
    float pumpPlanMl[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int c = 0; c < declaredChemicalCount && c < kMaxDeclaredChemicals; c++) {
        int pumpIdx = declaredChemicals[c].pumpIndex;
        if (pumpIdx >= 0 && pumpIdx < 4) pumpPlanMl[pumpIdx] = planMlPerDayByIndex[c];
    }
    json.set("plan/pump1", pumpPlanMl[0]);
    json.set("plan/pump2", pumpPlanMl[1]);
    json.set("plan/pump3", pumpPlanMl[2]);
    json.set("plan/pump4", pumpPlanMl[3]);

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

    clearDailyAccountingOnly("midnight-history-pushed");
    dailyAccountingDate = getTodayDateKey();
    saveDosingState();
    Serial.println("Midnight: daily stats/totals cleared and saved.");
    logger.println("Midnight: daily stats/totals cleared and saved.");
}

// handlePostManualTest() moved to lib/WebRoutes/WebRoutes.cpp

void setup() {
    // FIRST EXECUTABLE SAFETY ACTION:
    // Never allow LittleFS, logger startup, Preferences, Wi-Fi, or Firebase to
    // run before every active-HIGH pump output has been forced LOW.
    forcePumpPinsOffDirect();
    delay(5);
    forcePumpPinsOffDirect();

    Serial.begin(115200);

    // Capture the reset reason immediately, before network or logger startup.
    esp_reset_reason_t reason = esp_reset_reason();
    uint64_t chipid = ESP.getEfuseMac();

    Serial.println();
    Serial.println("================================");
    Serial.println("ESP32 BOOT INFORMATION");
    Serial.printf("RESET REASON: %s (%d)\n", resetReasonToString(reason), (int)reason);
    Serial.printf("DEVICE ID: %s\n", deviceID.c_str());
    Serial.printf("FIRMWARE: %s\n", FW_VERSION);
    Serial.printf("MAC Address: %s\n", WiFi.macAddress().c_str());
    Serial.printf("Chip ID: %04X%08X\n", (uint16_t)(chipid >> 32), (uint32_t)chipid);
    Serial.println("================================");

    delay(500);
    forcePumpPinsOffDirect();
    if (!LittleFS.begin(true)) {
        Serial.println("LITTLEFS ERROR: mount failed; boot history cannot be saved.");
    } else {
        saveBootRecordToLittleFS(reason, chipid);
    }
//LittleFS.format();
/*Serial.println("FORMATTING LITTLEFS LOG STORAGE NOW...");
WebSerial.println("FORMATTING LITTLEFS LOG STORAGE NOW...");
LittleFS.format();
Serial.println("LITTLEFS FORMAT DONE.");
WebSerial.println("LITTLEFS FORMAT DONE.");
esp_reset_reason_t reason = esp_reset_reason();*/



    // Register WebSerial early so any later WebSerial prints are safe.
    WebSerial.begin(&serialServer);

    // Filesystem queue enforcement happens inside logger.begin(). Reassert all
    // pump outputs OFF immediately before entering that code.
    forcePumpPinsOffDirect();
    logger.begin(deviceID, GOOGLE_LOG_URL);
    forcePumpPinsOffDirect();

    logger.println();
    logger.println("================================");
    logger.println("ESP32 BOOT INFORMATION");
    logger.printf("RESET REASON: %s (%d)\n", resetReasonToString(reason), (int)reason);
    logger.printf("DEVICE ID: %s\n", deviceID.c_str());
    logger.printf("FIRMWARE: %s\n", FW_VERSION);
    logger.printf("MAC Address: %s\n", WiFi.macAddress().c_str());
    logger.printf("Chip ID: %04X%08X\n", (uint16_t)(chipid >> 32), (uint32_t)chipid);
    logger.println("================================");

    WebSerial.println();
    WebSerial.println("================================");
    WebSerial.println("ESP32 BOOT INFORMATION");
    WebSerial.printf("RESET REASON: %s (%d)\n", resetReasonToString(reason), (int)reason);
    WebSerial.printf("DEVICE ID: %s\n", deviceID.c_str());
    WebSerial.printf("FIRMWARE: %s\n", FW_VERSION);
    WebSerial.printf("MAC Address: %s\n", WiFi.macAddress().c_str());
    WebSerial.printf("Chip ID: %04X%08X\n", (uint16_t)(chipid >> 32), (uint32_t)chipid);
    WebSerial.println("================================");

    printSavedBootHistory();

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

    // 30-second task watchdog. The old 120-second timeout could leave an
    // energized pump running far too long during a deadlock. Network code already
    // services the watchdog around long Firebase operations.
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);

    EEPROM.begin(512);

    prefs.begin("doser-settings", false);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    DOSING_THRESHOLD = prefs.getFloat("d_thresh", 1.0f);
    maxDoseLimit = prefs.getFloat("d_max", 15.0f);
    TANK_VOLUME_L = prefs.getFloat("t_vol", 1135.6f);
    prefs.end();

    runOneTimeKalkLimitMigration();
    loadLocalSettings();
    loadChemicalStrengths();
    loadAlkDemandLearningSetting();
    // loadFastAlkLearnerState() removed (v1 -> v2): the fastAlk learner it
    // restored no longer exists. See comment near runAiRecalculation().
    loadAlkDemandHistory();
    printAlkDemandRecommendation("boot");
    loadCalciumDemandLearningSetting();
    loadCalciumDemandHistory();
    printCalciumDemandRecommendation("boot");

    // Started unconditionally (unlike the emulator task, which only runs on
    // specific test devices) so a reachability signal is already being
    // established well before the 30-second Firebase-start grace period
    // elapses in loop(). Same task-isolation pattern as apexEmulatorTask
    // directly below -- pinned to core 0, never registered with the
    // watchdog, touches no Firebase objects.
    // RE-ENABLED 2026-08-05: the diagnostic test this block was built for
    // is complete and conclusive. This build (task fully disabled, zero
    // background DNS activity) still showed the same multi-second dashboard
    // delay -- which rules out internetCheckTask as the cause. The real
    // cause was found separately: server.handleClient() being called only
    // once, at the end of loop(), after several seconds of unconditional
    // chemistry-recalculation/logging work each pass (see the new
    // handleClient() call added above, near addCurrentAiPlanToBuckets()).
    // Restoring this task now since it's confirmed safe to run alongside
    // that fix -- Firebase needs it to ever connect.
    BaseType_t internetCheckTaskCreated = xTaskCreatePinnedToCore(
        internetCheckTask,
        "internetCheck",
        4096,
        nullptr,
        1,
        &internetCheckTaskHandle,
        0
    );

    if (internetCheckTaskCreated == pdPASS) {
        Serial.println("INTERNET CHECK TASK STARTED: connectToFirebase() will wait for a confirmed DNS path.");
        logger.println("INTERNET CHECK TASK STARTED: connectToFirebase() will wait for a confirmed DNS path.");
    } else {
        internetCheckTaskHandle = nullptr;
        internetReachable = true;
        Serial.println("INTERNET CHECK TASK FAILED TO START: falling back to unconditional Firebase start.");
        logger.println("INTERNET CHECK TASK FAILED TO START: falling back to unconditional Firebase start.");
    }

    if (useApexLogEmulatorForThisDevice()) {
        apexEmu.begin(APEX_EMULATOR_URL, APEX_EMULATOR_POLL_MS);

        BaseType_t taskCreated = xTaskCreatePinnedToCore(
            apexEmulatorTask,
            "apexEmulator",
            12288,
            nullptr,
            1,
            &apexEmulatorTaskHandle,
            0
        );

        if (taskCreated == pdPASS) {
            Serial.println("APEX EMU ASYNC TEST ENABLED: reefDoser3 reads reefDoser2 Drive logs without blocking loopTask.");
            logger.println("APEX EMU ASYNC TEST ENABLED: reefDoser3 reads reefDoser2 Drive logs without blocking loopTask.");
        } else {
            apexEmulatorTaskHandle = nullptr;
            Serial.println("APEX EMU ASYNC TASK FAILED TO START: emulator disabled for this boot.");
            logger.println("APEX EMU ASYNC TASK FAILED TO START: emulator disabled for this boot.");
        }
    }
    Serial.printf("BOOT PUMP SAFETIES LOADED: thresholds P1=%.2f P2=%.2f P3=%.2f P4=%.2f | maxDose P1=%.2f P2=%.2f P3=%.2f P4=%.2f | maxDay P1=%.2f P2=%.2f P3=%.2f P4=%.2f\n",
                  getPumpDoseThresholdMl(0), getPumpDoseThresholdMl(1), getPumpDoseThresholdMl(2), getPumpDoseThresholdMl(3),
                  getPumpMaxDoseMl(0), getPumpMaxDoseMl(1), getPumpMaxDoseMl(2), getPumpMaxDoseMl(3),
                  getPumpMaxDayMl(0), getPumpMaxDayMl(1), getPumpMaxDayMl(2), getPumpMaxDayMl(3));
    loadChemicalReservoirs();

    bool wifiConnected = false;

    if (ssid == "") {
        Serial.println("No WiFi saved. Starting Hotspot...");
        logger.println("No WiFi saved. Starting Hotspot...");
        provisioner.startPortal(("AIDoser-" + deviceID).c_str());
        state.transitionTo(SystemState::PROVISIONING);
    } else {
        Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
        logger.printf("Connecting to WiFi: %s\n", ssid.c_str());
        String localHostname = deviceID;
    localHostname.toLowerCase();

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(localHostname.c_str());

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

        if (MDNS.begin(localHostname.c_str())) {
            MDNS.addService("http", "tcp", 80);

            Serial.printf("mDNS started: http://%s.local\n", localHostname.c_str());
            logger.printf("mDNS started: http://%s.local\n", localHostname.c_str());
        } else {
            Serial.printf("mDNS FAILED: %s\n", localHostname.c_str());
            logger.printf("mDNS FAILED: %s\n", localHostname.c_str());
        }
            logger.println("\nWiFi Connected!");
            logger.print("IP Address: ");
            logger.println(WiFi.localIP().toString());
            logGoogleDriveDiagnostics("boot-wifi-connected");

            doser.begin();
            state.transitionTo(SystemState::IDLE);
        } else {
            Serial.println("\nWiFi Connection Failed. Reverting to Hotspot...");
            logger.println("\nWiFi Connection Failed. Reverting to Hotspot...");
            provisioner.startPortal("ReefDoser_Setup");
            state.transitionTo(SystemState::PROVISIONING);
        }
    }
    // Local dashboard HTTP routes are registered in lib/WebRoutes/WebRoutes.cpp
    registerWebRoutes();

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

    // §5 free chemical declaration: load the customer's declared chemical
    // list, or migrate one from the legacy dosingMode if this device has
    // never had chemicals.json before (true first boot on this firmware,
    // or an existing device updating from a pre-Phase-1 build). Must
    // happen before rebuildAiChemicalDeclarations() below, which reads
    // declaredChemicals[] directly.
    if (!loadDeclaredChemicals()) {
        Serial.println("CHEMICALS CONFIG: none found, migrating from legacy dosingMode.");
        logger.println("CHEMICALS CONFIG: none found, migrating from legacy dosingMode.");
        buildDeclaredChemicalsFromLegacyMode(dosingMode);
    }

    // §4.5 persistence: restore learned Kalman state from before this reboot,
    // before the first recalculation runs. Chemical declarations must exist
    // first so per-chemical confidence can be matched by NAME (§5.2) rather
    // than array index -- see PersistedStateV1's comment in AI_EngineV2.h.
    // Calling rebuildAiChemicalDeclarations() here is safe/idempotent even
    // though runAiRecalculation() below calls it again itself: that function
    // only rebuilds ai.chemicals[]/numChemicals, it never touches
    // ai.filters[], so the restored Kalman state survives that second call.
    ai.tank.tankVolumeLiters = TANK_VOLUME_L;
    ai.tank.coralLoad = coralLoadFromBaselineString(baselineCoralLoad);
    rebuildAiChemicalDeclarations();
    if (ai.restoreState()) {
        Serial.println("AI ENGINE V2: learned state restored from previous session.");
        logger.println("AI ENGINE V2: learned state restored from previous session.");
    } else {
        Serial.println("AI ENGINE V2: no saved learned state found (first boot, fresh state, or nothing to restore).");
        logger.println("AI ENGINE V2: no saved learned state found (first boot, fresh state, or nothing to restore).");
    }

    // Restore the live AI plan after reboot/OTA so /api/status and the local dashboard
    // do not show a zero dosing plan while saved bucket state still exists.
    if (hasSavedManualTest) {
        runAiRecalculation(
            currentAlk, currentCa, currentMg, currentPh,
            isLightsOn(), MeasurementSource::ManualTest
        );

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
    // First let the Doser service expire any timed pump that should already be OFF.
    // This prevents a normal completed dose from being mistaken for a runtime overrun
    // after Firebase, TLS, logger, or filesystem work temporarily delays the loop.
    doser.tick();

    // Then verify that no pump remains energized beyond its absolute safety deadline.
    servicePumpRuntimeSafety();
    yield();
    esp_task_wdt_reset(); // Tell the system "I'm still alive"

    // Execute OTA outside the Firebase stream callback. This prevents nested
    // Firebase/SSL work from stalling between command receipt and manifest download.
    if (pendingOtaRequested && !otaInProgress) {
        String firmwareUrl = pendingOtaUrl;
        String source = pendingOtaSource;

        pendingOtaRequested = false;
        pendingOtaUrl = "";
        pendingOtaSource = "";

        handleOtaFirmwareUrl(
            firmwareUrl,
            source.length() > 0 ? source.c_str() : "queued OTA"
        );

        // Successful OTA reboots before returning. A failed OTA is unblocked
        // inside handleOtaFirmwareUrl(). Do not run normal loop work this pass.
        return;
    }

    // Defensive gate: while the synchronous OTA download/write is active,
    // no scheduler, logger upload, AI, or Firebase stream work may run.
    if (otaInProgress) {
        doser.stopAllPumps();
        forcePumpPinsOffDirect();
        clearPumpRuntimeDeadline();
        return;
    }

    serviceInterPumpDelay();
    servicePumpRuntimeSafety();

    // OTA must not depend only on the Firebase stream. Poll the exact command
    // node as a fallback in case the stream is disconnected or misses its snapshot.
    serviceOtaCommandFallback();
    if (pendingOtaRequested) return;

    // Fixed 2026-08-05: server.handleClient() was only ever called once,
    // at the very end of loop() -- AFTER addCurrentAiPlanToBuckets() below,
    // which triggers chemistry recalculation and the multi-line
    // ALLOCATOR DIAGNOSTIC block seen throughout every log tonight. Each
    // of those lines goes through logger.println()/printf(), which does a
    // real LittleFS file open/write/close per line, not just a fast
    // Serial print. Confirmed via direct device-side timing instrumentation
    // that handleGetStatus() itself completes in ~40ms once actually
    // invoked -- the multi-second delay browsers were seeing as "waiting
    // for server response" was the REQUEST SITTING UNREAD on the socket
    // for however long the rest of this unconditional, ungated work took,
    // every single loop() pass, before handleClient() ever got called to
    // notice it. Calling it here too, before that heavy work starts, lets
    // a pending request get serviced promptly; the original call at the
    // end of loop() is left in place to catch anything that arrives during
    // the heavy work itself.
    server.handleClient();

    // Independent 10-minute AI dosing scheduler. This must run every loop pass;
    // Apex polling and hourly AI calculations only update the plan and must not
    // control whether a dosing slice is accumulated.
    addCurrentAiPlanToBuckets("Scheduler", false);

    // logger.loop() may rotate, upload, or delete LittleFS queue files.
    // Never perform those maintenance operations while any pump is energized.
    // Normal logger.printf() appends are still allowed; maintenance resumes
    // automatically as soon as all pumps are OFF.
    if (!anyDoserPumpRunning() && !apexTlsReservedOrCoolingDown()) {
        logger.loop();
    }

    // Fixed 2026-08-05: logger.loop() can attempt a real network upload
    // with its own 8-second bounded timeout. The earlier handleClient()
    // call above (before addCurrentAiPlanToBuckets()) doesn't help a
    // request that arrives DURING this specific window -- it still had to
    // wait for everything below to finish too, before the original
    // end-of-loop call would catch it. Confirmed via Network tab: /api/status
    // (served promptly by the earlier fix) dropped to 34ms, but the initial
    // document request -- the one most likely to land at an unlucky moment
    // on a fresh page load -- still showed ~3.9s almost entirely as
    // "waiting for server response," pointing at exactly this kind of gap.
    server.handleClient();

    if (millis() - lastGoogleDriveDiagMs >= GDRIVE_DIAG_INTERVAL_MS) {
        lastGoogleDriveDiagMs = millis();
        logGoogleDriveDiagnostics("loop");
    }

    // Remote logger heartbeat: this tiny Firebase node keeps updating even between
    // half-hour Google Drive log uploads, so a remote device can show the logger queue
    // building up before Drive logs go silent.
    publishLoggerHealthToFirebase("loop", false);

    // Stop calibration timed runs exactly by elapsed time.
    // This is independent of mL/min calibration and does not affect quick-dose dosing.
    unsigned long calNow = millis();
    for (int i = 0; i < 4; i++) {
        if (calibrationRunActive[i] && (long)(calNow - calibrationRunUntilMs[i]) >= 0) {
            doser.stopManualRun(i);
            calibrationRunActive[i] = false;
            clearPumpRuntimeDeadline();
            forcePumpPinsOffDirect();
            Serial.printf("Calibration timed run complete: pump %d\n", i + 1);
            logger.printf("Calibration timed run complete: pump %d\n", i + 1);
        }
    }

    server.handleClient();

    // Consume a completed emulator result in loopTask. The HTTPS request itself
    // runs on core 0. During its TLS reservation/cooldown, all other secure
    // Firebase and Drive work remains paused to avoid overlapping SSL engines.
    serviceApexEmulatorResult();

    static unsigned long lastTlsPauseLogMs = 0;
    if (apexTlsReservedOrCoolingDown() && millis() - lastTlsPauseLogMs > 10000UL) {
        lastTlsPauseLogMs = millis();
        long cooldownRemaining = (long)(apexTlsCooldownUntilMs - millis());
        if (cooldownRemaining < 0) cooldownRemaining = 0;
        Serial.printf("TLS SERIALIZER: main-loop network paused reservation=%d cooldownRemaining=%ld ms\n",
                      apexTlsReservation ? 1 : 0, cooldownRemaining);
    }

    // Delay Firebase startup so local dashboard can be reached first.
    // This preserves Firebase/OTA/commands but prevents SSL stream startup from starving port 80.
    //
    // Fixed 2026-08-05: added the internetReachable check. WiFi.status()
    // == WL_CONNECTED only means associated to the local router -- it says
    // nothing about whether DNS/internet actually works, which is exactly
    // the gap that let connectToFirebase() run into a DNS hang long enough
    // to trip the watchdog on a WiFi-but-no-internet network. This does
    // not change connectToFirebase() itself at all; it just waits for the
    // isolated internetCheckTask (above) to confirm a working path first.
    static unsigned long bootMs = millis();
    if (!firebaseStarted && WiFi.status() == WL_CONNECTED && internetReachable &&
        (millis() - bootMs > 30000UL)) {
        firebaseStarted = true;
        Serial.println("Starting Firebase after dashboard grace period...");
        logger.println("Starting Firebase after dashboard grace period...");
        connectToFirebase();
        publishLoggerHealthToFirebase("firebase-start", true);
        publishAlkDemandStatusToFirebase("firebase-start", true);
        publishCalciumDemandStatusToFirebase("firebase-start", true);
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

    // Evaluate alerts once per minute. Firebase writes only happen on state changes/cooldown.
    evaluateAlertState("loop", false);

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

    if (!apexTlsReservedOrCoolingDown() && firebaseStarted && WiFi.status() == WL_CONNECTED && Firebase.ready()) {
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

        // Do NOT restart immediately after WiFi credentials are saved.
        // The final setup page must stay alive long enough to show the real DHCP IP.
        if (provisioner.shouldRestart()) {
            Serial.println("Provisioning complete. Restarting after IP display window.");
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
    // Fair queue pointer. After a successful dose, the next service pass starts
    // with the following pump instead of always restarting at P1.
    static uint8_t nextPumpServiceIndex = 0;

    if (now - lastSliceMillis >= 600000UL) {
        lastSliceMillis = now;
            logger.println("\nWiFi Connected!");
            logger.print("IP Address: ");
            logger.println(WiFi.localIP().toString());
        doseSlotId++;

        float slotThresholds[4] = {
            getPumpDoseThresholdMl(0),
            getPumpDoseThresholdMl(1),
            getPumpDoseThresholdMl(2),
            getPumpDoseThresholdMl(3)
        };

        Serial.printf("[SLOT %lu] checking buckets: P1=%.2f P2=%.2f P3=%.2f P4=%.2f thresholds: P1=%.2f P2=%.2f P3=%.2f P4=%.2f\n",
                      doseSlotId, pumpBuckets[0], pumpBuckets[1], pumpBuckets[2], pumpBuckets[3],
                      slotThresholds[0], slotThresholds[1], slotThresholds[2], slotThresholds[3]);
        logger.printf("[SLOT %lu] checking buckets: P1=%.2f P2=%.2f P3=%.2f P4=%.2f thresholds: P1=%.2f P2=%.2f P3=%.2f P4=%.2f\n",
                         doseSlotId, pumpBuckets[0], pumpBuckets[1], pumpBuckets[2], pumpBuckets[3],
                         slotThresholds[0], slotThresholds[1], slotThresholds[2], slotThresholds[3]);

        for (int i = 0; i < 4; i++) {
            float pumpThreshold = slotThresholds[i];
            if (pumpBuckets[i] >= pumpThreshold) {
                Serial.printf("[SLOT %lu] PUMP %d queued: bucket %.2f ready to dose.\n",
                              doseSlotId, i + 1, pumpBuckets[i]);
                logger.printf("[SLOT %lu] PUMP %d queued: bucket %.2f ready to dose.\n",
                                 doseSlotId, i + 1, pumpBuckets[i]);
            } else {
                Serial.printf("[SLOT %lu] PUMP %d no dump: bucket %.2f below threshold %.2f\n",
                              doseSlotId, i + 1, pumpBuckets[i], pumpThreshold);
                logger.printf("[SLOT %lu] PUMP %d no dump: bucket %.2f below threshold %.2f\n",
                                 doseSlotId, i + 1, pumpBuckets[i], pumpThreshold);
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

    bool bootDosingGraceActive = (now < BOOT_DOSING_GRACE_MS);

    if (bootDosingGraceActive && !emergencyStop) {
        static unsigned long lastBootGraceLogMs = 0;
        bool bucketReadyDuringBootGrace = false;
        for (int i = 0; i < 4; i++) {
            if (pumpBuckets[i] >= getPumpDoseThresholdMl(i)) {
                bucketReadyDuringBootGrace = true;
                break;
            }
        }

        if (bucketReadyDuringBootGrace && (lastBootGraceLogMs == 0 || now - lastBootGraceLogMs >= 30000UL)) {
            lastBootGraceLogMs = now;
            Serial.printf("Dosing skipped: boot grace active for %lu more seconds. Buckets held: P1=%.2f P2=%.2f P3=%.2f P4=%.2f\n",
                          (unsigned long)((BOOT_DOSING_GRACE_MS - now) / 1000UL),
                          pumpBuckets[0], pumpBuckets[1], pumpBuckets[2], pumpBuckets[3]);
            logger.printf("Dosing skipped: boot grace active for %lu more seconds. Buckets held: P1=%.2f P2=%.2f P3=%.2f P4=%.2f\n",
                             (unsigned long)((BOOT_DOSING_GRACE_MS - now) / 1000UL),
                             pumpBuckets[0], pumpBuckets[1], pumpBuckets[2], pumpBuckets[3]);
        }
    } else if (otaInProgress) {
        static unsigned long lastOtaBlockLogMs = 0;
        doser.stopAllPumps();
        if (lastOtaBlockLogMs == 0 || now - lastOtaBlockLogMs >= 10000UL) {
            lastOtaBlockLogMs = now;
            Serial.println("OTA SAFETY: dosing scheduler blocked while OTA is in progress.");
            logger.println("OTA SAFETY: dosing scheduler blocked while OTA is in progress.");
        }
    } else if (!anyPumpRunning && !emergencyStop && interPumpDelayReady()) {
        // Round-robin service prevents a continuously ready P1/P2 bucket from
        // starving Eric's P3 NaOH or P4 Alk bucket. All existing safety caps,
        // one-pump-at-a-time behavior, and chemical-separation delay remain.
        for (int offset = 0; offset < 4; offset++) {
            const int i = (nextPumpServiceIndex + offset) % 4;
            if (pumpBuckets[i] >= getPumpDoseThresholdMl(i)) {
                float requestedBucketMl = pumpBuckets[i];
                float doseAmount = applyPumpSafetyCaps(i, requestedBucketMl, "AutoDose");
                if (doseAmount <= 0.0f) {
                    // Daily safety cap reached for this pump. Leave bucket intact and check the next pump.
                    continue;
                }

                // Arm the fail-safe before starting the pump so the runtime supervisor
                // can never observe a running pump without an active deadline.
                armPumpRuntimeDeadlineForMl(i, doseAmount, "AutoDose");

                float actualMl = doser.doseMl(i, doseAmount);
                if (actualMl <= 0.0f) {
                    clearPumpRuntimeDeadline();
                    Serial.printf("[SLOT %lu] PUMP %d dose failed to start; bucket held at %.2f ml\n",
                                  doseSlotId, i + 1, pumpBuckets[i]);
                    logger.printf("[SLOT %lu] PUMP %d dose failed to start; bucket held at %.2f ml\n",
                                  doseSlotId, i + 1, pumpBuckets[i]);
                    break;
                }

                noteInterPumpDoseStarted(i, "AutoDose");
                recordChemicalDispense(i, actualMl, "AutoDose");
                pumpBuckets[i] -= actualMl;
                if (pumpBuckets[i] < 0.01f) pumpBuckets[i] = 0.0f;

                saveDosingState();
                pumpDumpCount[i]++;
                nextPumpServiceIndex = static_cast<uint8_t>((i + 1) % 4);

                Serial.printf("[SLOT %lu] DUMP #%lu PUMP %d: requested %.2f ml, safety %.2f ml, actual %.2f ml, bucket now %.2f next=P%d\n",
                              doseSlotId, pumpDumpCount[i], i + 1, requestedBucketMl, doseAmount, actualMl, pumpBuckets[i], nextPumpServiceIndex + 1);
                logger.printf("[SLOT %lu] DUMP #%lu PUMP %d: requested %.2f ml, safety %.2f ml, actual %.2f ml, bucket now %.2f next=P%d\n",
                                 doseSlotId, pumpDumpCount[i], i + 1, requestedBucketMl, doseAmount, actualMl, pumpBuckets[i], nextPumpServiceIndex + 1);
                break;
            }
        }
    } else if (!anyPumpRunning && !emergencyStop && interPumpDelayActive) {
        static unsigned long lastInterPumpWaitLogMs = 0;
        if (lastInterPumpWaitLogMs == 0 || now - lastInterPumpWaitLogMs >= 30000UL) {
            lastInterPumpWaitLogMs = now;
            unsigned long remainingMs = 0;
            interPumpDelayReady(&remainingMs);
            const unsigned long remainingSec = (remainingMs + 999UL) / 1000UL;

            for (int i = 0; i < 4; i++) {
                if (pumpBuckets[i] >= getPumpDoseThresholdMl(i)) {
                    Serial.printf(
                        "[SLOT %lu] PUMP %d ready with %.2f ml, waiting %lu more seconds for chemical separation.\n",
                        doseSlotId, i + 1, pumpBuckets[i], remainingSec
                    );
                    logger.printf(
                        "[SLOT %lu] PUMP %d ready with %.2f ml, waiting %lu more seconds for chemical separation.\n",
                        doseSlotId, i + 1, pumpBuckets[i], remainingSec
                    );
                    break;
                }
            }
        }
    } else if (anyPumpRunning) {
        static unsigned long lastPumpQueueWaitLogMs = 0;
        if (now - lastPumpQueueWaitLogMs >= 5000UL) {
            lastPumpQueueWaitLogMs = now;
            for (int i = 0; i < 4; i++) {
                if (pumpBuckets[i] >= getPumpDoseThresholdMl(i)) {
                    Serial.printf("[SLOT %lu] PUMP %d ready to dose %.2f ml, waiting for active pump to finish.\n",
                                  doseSlotId, i + 1, pumpBuckets[i]);
                    logger.printf("[SLOT %lu] PUMP %d ready to dose %.2f ml, waiting for active pump to finish.\n",
                                     doseSlotId, i + 1, pumpBuckets[i]);
                    break;
                }
            }
        }
    }


    // Keep Firebase water parameters fresh even when Apex is disabled or a prior
    // Firebase write failed. The mirror timestamp advances only after success.
    static unsigned long lastStateMirrorRetryMs = 0;
    if (now - lastStateMirrorRetryMs >= 60000UL) {
        lastStateMirrorRetryMs = now;
        if (lastFirebaseMirrorMs == 0 || now - lastFirebaseMirrorMs >= FIREBASE_MIRROR_INTERVAL_MS) {
            mirrorStatusToFirebase();
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
        Serial.printf("[AI] Calculating plan for %.1fL volume...\n", TANK_VOLUME_L);
        logger.printf("[AI] Calculating plan for %.1fL volume...\n", TANK_VOLUME_L);

        // Use live Apex chemistry when available. The old code always used
        // lastLocalTest here; that stale pH/Ca created bogus NaOH/CaCl2 plans
        // and then Apex immediately overwrote them. This keeps one source of truth.
        calculateAiFromBestChemistry("Hourly");
        addCurrentAiPlanToBuckets("Hourly", false);
        publishAiPlanIfNeeded("Hourly", true);

        // Durable, searchable record of the maturity signal itself -- the
        // actual thing that matters, independent of whether the Manual Test
        // Schedule card is visible or anyone remembers to check the
        // dashboard. Watch these three maturity values over the coming
        // days: they should climb gradually (not instantly to 1.0, not
        // stuck at 0.0) as real measurements accumulate -- that's the real
        // evidence for whether today's Kalman fixes (maturity scale,
        // advanceTime(), repeat-poll gating) are actually working, not just
        // whether a UI card looks right.
        // "real=" is the live, legitimately-decaying value (dosing caution);
        // "proven=" is the ratchet (never regresses on its own) that
        // actually drives the manual-test-interval recommendation -- shown
        // side by side so a future "why did this drop" question is
        // answerable from this one line instead of a multi-day log dive.
        Serial.printf("[MATURITY] Alk real=%.3f/proven=%.3f Ca real=%.3f/proven=%.3f Mg real=%.3f/proven=%.3f | manualTestPrompt: daysSince=%d recommendedDays=%d governingMaturity=%.3f\n",
                      ai.filters[P_ALK].maturity(), ai.filters[P_ALK].provenMaturity(),
                      ai.filters[P_CA].maturity(), ai.filters[P_CA].provenMaturity(),
                      ai.filters[P_MG].maturity(), ai.filters[P_MG].provenMaturity(),
                      daysSinceLastManualTest(), recommendedManualTestIntervalDays(), manualTestGoverningMaturity());
        logger.printf("[MATURITY] Alk real=%.3f/proven=%.3f Ca real=%.3f/proven=%.3f Mg real=%.3f/proven=%.3f | manualTestPrompt: daysSince=%d recommendedDays=%d governingMaturity=%.3f\n",
                      ai.filters[P_ALK].maturity(), ai.filters[P_ALK].provenMaturity(),
                      ai.filters[P_CA].maturity(), ai.filters[P_CA].provenMaturity(),
                      ai.filters[P_MG].maturity(), ai.filters[P_MG].provenMaturity(),
                      daysSinceLastManualTest(), recommendedManualTestIntervalDays(), manualTestGoverningMaturity());
    }

    static unsigned long lastApexPull = 0;
    if (apexEnabled && !useApexLogEmulatorForThisDevice() &&
        (millis() - lastApexPull > 300000UL)) {
        lastApexPull = millis();
        syncAllTruths();
    }

    if (currentTempF > 84.0f && !emergencyStop) {
        triggerEmergencyStop(
            "Local temperature exceeded 84.0 F (reading=" + String(currentTempF, 2) + " F)",
            "LocalSensor"
        );
    }

    if (currentPh > 11.0f && !emergencyStop) {
        triggerEmergencyStop(
            "Local pH exceeded 11.0 (reading=" + String(currentPh, 2) + ")",
            "LocalSensor"
        );
    }

    // 1. UPDATE AVERAGES (Every 5 mins)
    static unsigned long lastAvgUpdate = 0;
    if (millis() - lastAvgUpdate > 300000UL) {
        lastAvgUpdate = millis();

        if (currentAlk > 0.0f && currentPh > 0.0f && newChemistrySamplePendingForDailyStats) {
            newChemistrySamplePendingForDailyStats = false;
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
        } else if (currentAlk <= 0.0f || currentPh <= 0.0f) {
            Serial.println("[STATS] skipped: no valid Alk/pH yet");
            logger.println("[STATS] skipped: no valid Alk/pH yet");
        } else {
            Serial.println("[STATS] no new chemistry measurement; learner history unchanged");
            logger.println("[STATS] no new chemistry measurement; learner history unchanged");
        }
    }

    // 2. MIDNIGHT PUSH
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
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