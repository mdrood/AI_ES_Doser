// =============================================================================
// AIDoser TEST-ONLY: wipe the setup-wizard data so the 7-step wizard on the
// local dashboard runs again from Step 1.
//
// Compiled ONLY when the build defines AIDOSER_TEST_BUILD=1 (platformio.ini,
// test environment only). Customer firmware: leave the flag out and this whole
// file compiles to nothing.
//
// Two ways to trigger it:
//   1) POST /api/test/reset-setup            (local, registered in WebRoutes.cpp)
//   2) Firebase command from the reset Cloud Function:
//        devices/<id>/commands/testReset = { trigger:true, nonce:"...", ... }
//      aidoserTestPoll() (call it from loop()) sees it, runs the wipe, and
//      writes the same nonce to .../testReset/ack so the cloud knows it worked.
//
// KEEPS: Wi-Fi credentials, device ID, firmware, history, pump flow-rate
//        calibration values (see NOTE 4), pH target range.
// =============================================================================
#include "WebRoutesShared.h"
#include <LittleFS.h>

#if defined(AIDOSER_TEST_BUILD) && AIDOSER_TEST_BUILD

bool aidoserTestResetSetup() {
    Serial.println("TEST RESET: wiping setup-wizard data");
    logger.printf("TEST RESET: wiping setup-wizard data\n");

    // 1) Nothing may dose while setup is empty.
    for (int i = 0; i < 4; i++) pumpBuckets[i] = 0.0f;
    alkBucket = 0.0f;
    for (int i = 0; i < kMaxDeclaredChemicals; i++) planMlPerDayByIndex[i] = 0.0f;
    currentPlan = LegacyPlanView();

    // 2) Declared chemicals ("What Are You Dosing?"). Cleared directly, NOT via
    //    ai.removeChemical(): its sufficiency check can refuse and leave some behind.
    for (int i = 0; i < kMaxDeclaredChemicals; i++) declaredChemicals[i] = DeclaredChemical();
    declaredChemicalCount = 0;
    bool saved = saveDeclaredChemicals();      // writes /config/chemicals.json
    rebuildAiChemicalDeclarations();           // resyncs ai.chemicals[] / numChemicals
    ai.numChemicals = 0;

    // 3) Tank volume + coral load + cold-start baselines (wizard Step 2).
    TANK_VOLUME_L = 1135.6f;                   // same default /api/config/volume falls back to
    ai.tank.tankVolumeLiters = TANK_VOLUME_L;
    baselineKalkMlDay  = 0.0f;
    baselineCacl2MlDay = 0.0f;
    baselineNaohMlDay  = 0.0f;
    baselineMgMlDay    = 0.0f;
    baselineCoralLoad  = "moderate";           // the wizard's default selection
    prefs.begin("doser-settings", false);
    prefs.putFloat("t_vol", TANK_VOLUME_L);
    prefs.putFloat("base_kalk", 0.0f);
    prefs.putFloat("base_cacl2", 0.0f);
    prefs.putFloat("base_naoh", 0.0f);
    prefs.putFloat("base_mg", 0.0f);
    prefs.putString("base_load", baselineCoralLoad);
    prefs.end();
    applyAiBaselineToEngine();

    // 4) Chemistry targets back to the wizard's own "recommended defaults"
    //    (Alk 8.5 / Ca 450 / Mg 1440).
    //    NOTE: pump flow-rate calibration is left as-is on purpose. I don't
    //    know a safe "uncalibrated" value for saveFlowRate(), and a 0 could
    //    divide by zero in the dose-timing code. The wizard's Step 4 simply
    //    overwrites each pump's flow rate when the tester calibrates again.
    saveChemistryTargets(8.5f, 450.0f, 1440.0f);

    // 5) Baseline test + learned state ("no dosing before the baseline test").
    hasSavedManualTest = false;
    for (int p = 0; p < kNumParams; p++) ai.filters[p] = ParamKalmanState();
    alkDemandStore = AlkDemandStore();
    calciumDemandStore = CalciumDemandStore();
    if (LittleFS.exists(ALK_DEMAND_HISTORY_FILE)) LittleFS.remove(ALK_DEMAND_HISTORY_FILE);
    if (LittleFS.exists(CA_DEMAND_HISTORY_FILE))  LittleFS.remove(CA_DEMAND_HISTORY_FILE);

    // 6) Finally, the flag that makes the wizard come back.
    saveSetupWizardCompleted(false);

    Serial.printf("TEST RESET: done (chemicals saved=%s). Reload the dashboard.\n", saved ? "yes" : "NO");
    logger.printf("TEST RESET: done (chemicals saved=%s)\n", saved ? "yes" : "NO");
    return saved;
}

// Call from loop(). Cheap: checks Firebase at most once every 3 seconds.
void aidoserTestPoll() {
    static unsigned long lastCheckMs = 0;
    static String lastHandledNonce = "";
    if (millis() - lastCheckMs < 3000UL) return;
    lastCheckMs = millis();

    if (WiFi.status() != WL_CONNECTED || !firebaseStarted || deviceID.length() == 0) return;

    String base = "/devices/" + deviceID + "/commands/testReset";

    // Absent node (the normal case) makes these calls fail -> just return.
    if (!Firebase.getBool(writeFbdo, (base + "/trigger").c_str())) return;
    if (!writeFbdo.boolData()) return;
    if (!Firebase.getString(writeFbdo, (base + "/nonce").c_str())) return;
    String nonce = writeFbdo.stringData();
    if (nonce.length() == 0 || nonce == lastHandledNonce) return;

    lastHandledNonce = nonce;
    bool ok = aidoserTestResetSetup();

    // Only confirm on success: no ack -> the cloud function reports
    // "device did not confirm" and leaves the cloud untouched.
    if (ok) Firebase.setString(writeFbdo, (base + "/ack").c_str(), nonce);
    Firebase.setBool(writeFbdo, (base + "/trigger").c_str(), false);
}

#endif // AIDOSER_TEST_BUILD
