#pragma once

// This header exists so WebRoutes.cpp (the extracted HTTP handlers) can see
// exactly the same globals, structs, and helper functions that main.cpp
// already defines. Nothing here changes behavior - it only makes existing
// main.cpp symbols visible to a second .cpp file.
//
// main.cpp keeps the real variable DEFINITIONS exactly where they were.
// This header only adds `extern` declarations (and, for a few structs that
// used to be defined inline in main.cpp, the one true definition - moved
// here so both main.cpp and WebRoutes.cpp see the same type).

#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include <FirebaseESP32.h>
#include <ArduinoJson.h>

#include "Doser.h"
#include "AI_EngineV2.h"
#include "Allocator.h"
#include "ChemicalPresets.h"
#include "StateMachine.h"
#include "Provisioner.h"
#include "logger.h"
#include "apexapi.h"
#include "ApexLogEmulator.h"
#include "Dashboard.h"

// NOTE: we intentionally do NOT #include "global_config.h" here.
// PlatformIO's Library Dependency Finder does not add the project's
// top-level include/ folder to a lib/ subfolder's include path the way it
// does for src/, so that include fails to resolve from inside lib/WebRoutes.
// These are the specific global_config.h externs the route handlers
// actually use - duplicate extern declarations of the same globals are
// legal in C++, so this doesn't conflict with main.cpp also including the
// real global_config.h.
extern bool emergencyStop;
extern int dosingMode;
extern float currentTempF;
extern float currentPh;
extern float currentCond;
extern float currentPpt;
extern float currentSg;
extern float currentAlk;
extern float currentCa;
extern float currentMg;
extern float pumpFlowRates[4];

// ---------------------------------------------------------------------
// Structs previously defined inline in main.cpp. Moved here (unchanged)
// so the type is available wherever it's needed. main.cpp still owns the
// actual global instances (lightConfig, alkDemandStore, calciumDemandStore).
// ---------------------------------------------------------------------

struct LightConfig {
    int source = 0;    // 0 = Timer, 1 = Apex
    int start = 8;      // 8 AM
    int end = 20;        // 8 PM
    String outlet = "Light_7_1";
};

static constexpr uint32_t ALK_DEMAND_MAGIC = 0x414C4B37UL; // "ALK7"
static constexpr uint8_t ALK_DEMAND_VERSION = 3;
static constexpr uint8_t ALK_DEMAND_MAX_DAYS = 14;

struct AlkDemandDay {
    uint32_t dayKey = 0;       // local YYYYMMDD
    float avgAlk = 0.0f;
    float kalkMl = 0.0f;
    float afrMl = 0.0f;
    float naohMl = 0.0f;
    float alkMl = 0.0f;
    uint8_t mode = 0;
    uint8_t reserved[3] = {0,0,0};
};

struct AlkDemandStore {
    uint32_t magic = ALK_DEMAND_MAGIC;
    uint8_t version = ALK_DEMAND_VERSION;
    uint8_t count = 0;
    uint8_t seeded = 0;
    uint8_t reserved = 0;
    float recommendedDailyDemandDkh = 0.0f;
    float lastRecommendedP4MlDay = 0.0f;
    AlkDemandDay days[ALK_DEMAND_MAX_DAYS];
};

static constexpr uint32_t CA_DEMAND_MAGIC = 0x43413744UL; // "CA7D"
static constexpr uint8_t CA_DEMAND_VERSION = 2;
static constexpr uint8_t CA_DEMAND_MAX_DAYS = 7;

struct CalciumDemandDay {
    uint32_t dayKey = 0;
    float avgCa = 0.0f;
    float kalkMl = 0.0f;
    float afrMl = 0.0f;
    float cacl2Ml = 0.0f;
    uint8_t mode = 0;
    uint8_t reserved[3] = {0,0,0};
};

struct CalciumDemandStore {
    uint32_t magic = CA_DEMAND_MAGIC;
    uint8_t version = CA_DEMAND_VERSION;
    uint8_t count = 0;
    uint8_t reserved1 = 0;
    uint8_t reserved2 = 0;
    float recommendedDailyDemandPpm = 0.0f;
    float lastRecommendedP2MlDay = 0.0f;
    CalciumDemandDay days[CA_DEMAND_MAX_DAYS];
};

struct GoogleDriveLogQueueStats {
    int fileCount = 0;
    size_t totalBytes = 0;
    String oldestPath = "none";
    size_t oldestSize = 0;
    String newestPath = "none";
    size_t newestSize = 0;
};

// ---------------------------------------------------------------------
// v1 -> v2 AI engine compatibility view (see main.cpp's "v1 -> v2
// compatibility layer" comment block for the full explanation). Moved here
// -- same reasoning as AlkDemandStore/CalciumDemandStore above -- so
// WebRoutes.cpp and main.cpp share the exact same type. main.cpp still owns
// the one real `currentPlan` instance (see extern below).
// ---------------------------------------------------------------------
enum LegacyChemSlot : int {
    SLOT_KALK  = 0,
    SLOT_AFR   = 1,
    SLOT_ALK   = 2,
    SLOT_CACL2 = 3,
    SLOT_NAOH  = 4,
    SLOT_MG    = 5,
    kLegacyChemCount = 6
};

struct LegacyPlanView {
    float kalk = 0.0f, afr = 0.0f, alk = 0.0f, cacl2 = 0.0f, naoh = 0.0f, mg = 0.0f;
    bool active = false;
};

struct dailyStats {
    float tempSum = 0.0f;
    float phSum = 0.0f;
    float alkSum = 0.0f;
    float caSum = 0.0f;
    float mgSum = 0.0f;
    float pptSum = 0.0f;
    float sgSum = 0.0f;
    int count = 0;
};

// ---------------------------------------------------------------------
// Free chemical declaration (§5 "customer declares what they're dosing" --
// replaces the mode picker). One entry per physical pump: owner decision
// 2026-07-24 was to always require a pump assignment, so a chemical never
// exists mid-air unwired. kMaxDeclaredChemicals matches the hardware's
// fixed 4 physical pumps (SAFETY_PUMP_PINS[4] in main.cpp), not the
// engine's kMaxChemicals=8 (which allows headroom this integration layer
// deliberately doesn't use, per that same decision).
// ---------------------------------------------------------------------
static constexpr int kMaxDeclaredChemicals = 4;

struct DeclaredChemical {
    String id;                    // stable id, survives reorder/edits
    String name;                  // customer-facing label
    int presetId = -1;            // -1 = custom; else (int)ChemicalPresetId
    float potencyAlkPerMl = 0.0f; // dKH/mL -- THIS tank's actual per-mL potency
    float potencyCaPerMl = 0.0f;  // ppm/mL
    float potencyMgPerMl = 0.0f;  // ppm/mL
    // Added 2026-08-04, at owner's direct observation: pH was structurally
    // unable to be weighed against Alk/Ca/Mg in the allocator's own math --
    // rebuildAiChemicalDeclarations() unconditionally hardcoded every
    // chemical's pH potency to 0, so the same NNLS solve that already
    // correctly trades off Kalk vs. CaCl2 for Ca had nothing real to work
    // with for pH, no matter how much real-world data existed. 0.0f (the
    // default) is honest and safe -- "no known pH effect declared yet,"
    // same as every chemical's real behavior before this field existed --
    // not "this chemical genuinely has zero pH effect."
    // Units: pH units per mL (this tank's actual per-mL potency, same
    // convention as the three above -- not a per-gallon preset constant).
    float potencyPhPerMl = 0.0f;
    bool phSensitive = false;
    int pumpIndex = -1;           // 0-3, always assigned for a real/saved entry
    float maxMlPerDay = 0.0f;     // §7 safety cap
    bool active = true;

    // Added 2026-08-04, at owner's explicit direction: "all chemistries
    // should have a max day and a max bucket accumulator... this system
    // was SUPPOSED to work the same for all chemistries regardless which
    // ones you are dosing or not." Root problem this closes: bucket
    // threshold, max single dose, and (critically) the PHYSICAL EXECUTION
    // enforcement of max/day all lived on the physical pump slot
    // (pumpDoseThresholdMl[]/pumpMaxDoseMl[]/pumpMaxDayMl[], indexed 0-3),
    // completely separate from this chemical's own maxMlPerDay that the
    // ALLOCATOR plans against. Since every declared chemical maps 1:1 to
    // exactly one physical pump, there was never a hardware reason for
    // that split -- it's a leftover from v1's mode/pump-numbered thinking.
    // Confirmed via the reefDoser12 Calcium incident that this split is a
    // real, live bug, not just an inconsistency: editing this chemical's
    // maxMlPerDay (allocator planning ceiling) without ALSO editing the
    // separate pump-level array (physical dispensing enforcement, see
    // applyPumpSafetyCaps() -> getPumpMaxDayMl()) leaves the two silently
    // disagreeing, and the smaller one wins at the physical pump
    // regardless of what the allocator planned.
    //
    // 0.0f (the default) means "not yet configured -- fall back to
    // whatever this pump's array-based value currently is," exactly the
    // same migration-safe pattern maxMlPerDay itself already uses. See
    // getPumpDoseThresholdMl()/getPumpMaxDoseMl()/getPumpMaxDayMl() in
    // main.cpp for where this now takes priority over the old arrays.
    float bucketThresholdMl = 0.0f;  // mL that must accumulate before this chemical's pump fires
    float maxSingleDoseMl = 0.0f;    // mL cap for one physical pump run

    // Added 2026-07-27, generalized across all dosing (not one-off): what
    // fraction of this chemical's daily total gets dispensed during
    // lights-OFF hours, 0-100. Default 50 = flat/even across the whole day
    // (today's existing behavior, unchanged for anything that doesn't set
    // this). This is a scheduler-level TIME redistribution of an already-
    // decided daily amount -- the allocator's NNLS solve and safety caps
    // are completely unaffected; only WHEN within the day the same total
    // gets physically dispensed changes. See addCurrentAiPlanToBuckets()'s
    // nightWeightedSliceMultiplier() for the actual redistribution math,
    // a direct generalization of V1's proven getKalkDayNightSliceMultiplier()
    // (which only ever applied to one hardcoded chemical) to work for any
    // declared chemical on any tank.
    float nightFraction = 50.0f;

    // Added 2026-07-28, see ChemicalDeclaration::daytimeSuppressPercent for
    // the full explanation -- generalizes V1's Mode-7-only hard day/night
    // Alk split into something any phSensitive chemical can use. Default 0
    // = no change from existing behavior.
    float daytimeSuppressPercent = 0.0f;
};

extern DeclaredChemical declaredChemicals[kMaxDeclaredChemicals];
extern int declaredChemicalCount;

// Loads/saves /config/chemicals.json (LittleFS). Both non-fatal on
// failure: load leaves the in-memory list as whatever it already was
// (empty on true first boot -- the migration path below handles that);
// save logs a warning but never blocks the calling request.
bool loadDeclaredChemicals();
bool saveDeclaredChemicals();

// Builds declaredChemicals[]/declaredChemicalCount from a legacy
// dosingMode number (reuses the same pump-wiring knowledge the old
// SlotSpec table encoded). Used two ways: (1) the one-time boot migration
// when chemicals.json doesn't exist yet, and (2) the /api/dosing-mode
// compatibility shim every time the OLD dashboard's mode picker is used
// during the Phase 1-3 transition, so both dashboards stay consistent
// with whichever was touched most recently.
void buildDeclaredChemicalsFromLegacyMode(int mode);

// Rebuilds ai.chemicals[]/numChemicals from declaredChemicals[] -- the
// replacement for the old SlotSpec/dosingMode-keyed version. Also called
// by the WebRoutes chemical-management handlers after any add/edit/remove.
void rebuildAiChemicalDeclarations();

int findDeclaredChemicalIndexById(const String& id);
bool isPumpIndexTaken(int pumpIndex, const String& excludeId);
String generateChemicalId();

// ---------------------------------------------------------------------
// Globals used by the extracted web route handlers (extern only -
// definitions remain in main.cpp exactly where they already were).
// ---------------------------------------------------------------------

extern const char* APEX_EMULATOR_URL;
extern const float ML_PER_GALLON;

extern struct dailyStats dailyStats;
extern float dailyDoseTotals[4]; // defined in lib/Doser/Doser.cpp
extern float DOSING_THRESHOLD;
extern float maxDoseLimit;
extern float recipeKalkGpg;
extern float recipeAfrGpg;
extern float recipeAlkGpg;
extern float recipeNaohGpg;
extern float recipeMgGpg;
extern float recipeCacl2Gpg;

extern AIEngineV2 ai;
// Added 2026-08-04: needed so handlePostLiveDose() (WebRoutes.cpp) can map
// a physical pump index to its declared-chemical index, to call
// ai.recordManualDose() -- defined in main.cpp (built earlier for the
// per-chemical threshold/cap work), previously only forward-visible
// within main.cpp itself.
int chemicalIndexForPump(int pumpIndex);
extern LegacyPlanView currentPlan;
// v1 -> v2: chemical strengths now live in main.cpp as plain globals
// (no more ai.getDkhPerMlKalk()/etc accessors -- see main.cpp's
// "v1 -> v2 compatibility layer" comment).
extern float kalkStrengthDkhPerMl;
extern float afrStrengthDkhPerMl;
extern float alkStrengthDkhPerMl;
extern float naohStrengthDkhPerMl;
extern float mgStrengthPpmPerMl;
extern float cacl2StrengthPpmPerMl;
extern float aiMaxAlkDayMl;
extern float aiMaxAlkRiseDkhDay;
extern float aiMaxCaRisePpmDay;
extern float aiMaxKalkDayMl;
extern float aiMaxMgCorrectionDayMl;
extern float aiMaxMgDayMl;
extern float aiMaxNaohDayMl;
extern float aiMgDeadbandPpm;
extern float alkBucket;
extern AlkDemandStore alkDemandStore;
extern ApexApi apex;
extern ApexLogEmulator apexEmu;
extern bool apexEnabled;
extern String apexIp;
extern bool automaticCalciumLearningEnabled;
extern bool automaticDemandLearningEnabled;
extern float baselineCacl2MlDay;
extern String baselineCoralLoad;
extern float baselineKalkMlDay;
extern float baselineMgMlDay;
extern float baselineNaohMlDay;
extern CalciumDemandStore calciumDemandStore;
extern bool calibrationRunActive[4];
extern unsigned long calibrationRunUntilMs[4];
extern float chemicalCapacityGal[4];
extern float chemicalRemainingMl[4];
extern bool currentAlertActive;
extern String currentAlertCode;
extern String currentAlertLevel;
extern String currentAlertMessage;
extern String deviceID;
extern Doser doser;
extern String emergencyStopReason;
extern bool firebaseStarted;
extern bool hasSavedManualTest;
extern LightConfig lightConfig;
extern float mode7DayAlkPct;
extern float mode7DayNaohPct;
extern bool mode7DayNightSplitEnabled;
extern float mode7NaohMaxPh;
extern float mode7NightAlkPct;
extern float mode7NightNaohPct;
extern String notificationLevel;
extern bool otaInProgress;
extern Preferences prefs;
extern float pumpBuckets[4];
extern float pumpDoseThresholdMl[4];
extern float pumpMaxDayMl[4];
// §5 free chemical declaration: mode-agnostic plan values, index-aligned
// with declaredChemicals[] (see main.cpp's rebuildAiChemicalDeclarations()
// comment on why that alignment matters). Used by handleGetStatus() to
// report the plan by physical pump index instead of assuming one of the
// six legacy chemical names.
extern float planMlPerDayByIndex[kMaxDeclaredChemicals];
extern float pumpMaxDoseMl[4];
extern String recipeAfrType;
extern String recipeAlkType;
extern String recipeCacl2Type;
extern String recipeMgType;
extern WebServer server;
extern StateManager state;
extern int systemMode;
extern bool tankVolumePublishedThisBoot;
extern float TANK_VOLUME_L;
extern FirebaseData writeFbdo;

// ---------------------------------------------------------------------
// Helper/business-logic functions (defined in main.cpp) that the web
// route handlers call into. Some of these already have a forward
// declaration elsewhere in main.cpp - duplicate matching declarations
// are legal in C++ and harmless.
// ---------------------------------------------------------------------

bool acceptNewChemistryMeasurement(const char* source, const String& measurementId, bool forceNew);
void addCurrentAiPlanToBuckets(const char* source, bool force);
bool anyDoserPumpRunning();
static constexpr const char* ALK_DEMAND_HISTORY_FILE = "/alk_demand.bin";
static constexpr const char* CA_DEMAND_HISTORY_FILE = "/ca_demand.bin";
static constexpr float CA_PPM_PER_DKH_KALK = 7.142857f;

uint32_t localDayKeyWithOffsetDays(int offsetDays);

void applyAiBaselineToEngine();
void applyAiChemistrySafetiesToEngine();
void applyMode7DayNightSplitToEngine();
float applyPumpSafetyCaps(int pumpIndex, float requestedMl, const char* source);
void armPumpRuntimeDeadlineForMl(int pumpIndex, float ml, const char* source);
void armPumpRuntimeDeadlineMs(int pumpIndex, unsigned long requestedRunMs, const char* source);
// isNewMeasurement (default false): only pass true from a call site that
// represents a genuinely new physical measurement (manual test entry, a
// real changed Apex/Trident result) -- see the definition's comment in
// main.cpp for why this matters (fixed 2026-07-24: previously every call
// re-fed the Kalman filter regardless, inflating maturity from repeated
// polls of unchanged data).
void calculateAiFromBestChemistry(const char* sourceLabel, bool isNewMeasurement = false);
void clearPumpRuntimeDeadline();
void evaluateAlertState(const char* source, bool force);
float getPumpDailyRemainingMl(int pumpIndex);
float getPumpDoseThresholdMl(int pumpIndex);
float getPumpMaxDayMl(int pumpIndex);
float getPumpMaxDoseMl(int pumpIndex);
bool interPumpDelayReady(unsigned long* remainingMs);
GoogleDriveLogQueueStats collectGoogleDriveLogQueueStats();
void logGoogleDriveDiagnostics(const char* source);
bool publishLoggerHealthToFirebase(const char* source, bool force);
bool isLightsOn();
bool isValidDosingMode(int mode);
// §1/§3.2 manual-test-prompt feature (manual-only tanks only -- see
// isManualOnlyTank()'s comment in main.cpp for why Apex-equipped tanks are
// excluded). Engine tracks/computes; dashboard reads and renders.
bool isManualOnlyTank();
float manualTestGoverningMaturity();
int recommendedManualTestIntervalDays();
int daysSinceLastManualTest();
// §8/§8.5 one-time setup wizard (local dashboard).
extern bool setupWizardCompleted;
bool shouldShowSetupWizard();
void saveSetupWizardCompleted(bool completed);
// §8.5 chemistry targets -- previously hardcoded compile-time constants
// with no customer-facing way to change them at all.
extern float targetAlk;
extern float targetCa;
extern float targetMg;
void saveChemistryTargets(float alk, float ca, float mg);
bool isValidNotificationLevel(const String& level);
bool isValidSystemMode(int mode);
bool mirrorStatusToFirebase();
void noteInterPumpDoseStarted(int pumpIndex, const char* source);
void publishAiPlanIfNeeded(const char* source, bool force);
bool publishAlertState(const String& level, const String& code, const String& message, bool active, const char* source, bool force);
bool publishAlkDemandStatusToFirebase(const char* source, bool force);
bool publishCalciumDemandStatusToFirebase(const char* source, bool force);
bool publishTankVolumeToFirebase(const char* source);
int pumpCountForCurrentDosingMode();
const char* pumpKeyForPhysicalIndex(int idx);
void recordChemicalDispense(int pumpIndex, float ml, const char* source);
float sanitizePercent(float value, float fallback);
float sanitizePhCutoff(float value, float fallback);
void saveAiChemistrySafeties();
void saveAlkDemandLearningSetting();
void saveCalciumDemandLearningSetting();
void saveChemicalRecipes();
void saveChemicalReservoirs();
void saveFlowRate(int idx, float flowMlPerMin);
void saveManualTestLocally(float alk, float ca, float mg, float ph);
void saveMode7DayNightSplit();
void savePumpSafeties();
void syncAllTruths();
void triggerEmergencyStop(const String& reason, const char* source);
bool useApexLogEmulatorForThisDevice();

// handleOtaFirmwareUrl lives in main.cpp and is called from the local
// dashboard's manual "trigger OTA" testing endpoint if present.
bool handleOtaFirmwareUrl(const String& firmwareUrl, const char* source);
