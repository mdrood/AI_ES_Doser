#include "WebRoutes.h"
#include "WebRoutesShared.h"

// The functions below are moved verbatim from src/main.cpp (see
// git history / the original monolithic main.cpp for provenance).
// Behavior is unchanged - only the file they live in changed.

// Removed 2026-08-04: handleGetAlkDemandLearning, handleGetCalciumDemandLearning,
// handlePostCalciumDemandLearning, handlePostAlkDemandLearning, and
// handlePostChemicalStrengths -- confirmed via explicit source inspection
// (each function's full body read, checked for any ai./safety./tank.
// assignment) that none of these touch anything live in v2. Corresponding
// Dashboard.h UI/JS was already removed. Their route registrations below
// are removed in the same edit. NOT removed alongside these (confirmed
// mixed dead/live, still load-bearing): handlePostAiBaseline (coralLoad
// feeds ai.tank.coralLoad), handlePostMode7DayNightSplit (naohMaxPh feeds
// ai.safety.naohPhCeiling), handlePostAiChemistrySafeties (3 of 8 fields
// feed the real v2 safety envelope -- see Dashboard.h's restored "Dosing
// Safety Rise Limits" card for the corrected, accurately-scoped UI).

void handleGetStatus() {
    // TEMP DIAGNOSTIC (2026-08-05): timing instrumentation to locate the
    // remaining ~4-5s delay after fixing the getLocalTime() default-
    // timeout issue (11s -> 4-5s). Reading every function this handler
    // calls found no second blocking call -- this measures where the
    // actual time goes instead of guessing further. Remove once resolved.
    unsigned long __t0 = millis();
    JsonDocument doc;
    doc["ok"] = true;
    doc["deviceId"] = deviceID;
    doc["notificationLevel"] = notificationLevel;
    doc["alertState"]["active"] = currentAlertActive;
    doc["alertState"]["level"] = currentAlertLevel;
    doc["alertState"]["code"] = currentAlertCode;
    doc["alertState"]["message"] = currentAlertMessage;
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
    doc["apexEmulator"]["enabled"] = useApexLogEmulatorForThisDevice();
    doc["apexEmulator"]["urlConfigured"] = String(APEX_EMULATOR_URL).indexOf("YOUR_DEPLOYMENT_ID") < 0;
    doc["apexEmulator"]["lastPollMs"] = apexEmu.lastPollMs();
    doc["apexEmulator"]["lastGoodMs"] = apexEmu.lastGoodMs();
    doc["apexEmulator"]["sourceDevice"] = apexEmu.chemistry().deviceId;
    doc["apexEmulator"]["logTime"] = apexEmu.chemistry().logTime;
    doc["apexEmulator"]["error"] = apexEmu.chemistry().error;
    doc["lightConfig"]["source"] = lightConfig.source;
    doc["lightConfig"]["start"] = lightConfig.start;
    doc["lightConfig"]["end"] = lightConfig.end;
    doc["lightConfig"]["outlet"] = lightConfig.outlet;
    doc["aiBaseline"]["kalk"] = baselineKalkMlDay;
    doc["aiBaseline"]["cacl2"] = baselineCacl2MlDay;
    doc["aiBaseline"]["naoh"] = baselineNaohMlDay;
    doc["aiBaseline"]["mg"] = baselineMgMlDay;
    doc["aiBaseline"]["coralLoad"] = baselineCoralLoad;
    doc["alkDemandLearning"]["enabled"] = automaticDemandLearningEnabled;
    doc["alkDemandLearning"]["daysCollected"] = (int)min((uint8_t)7, alkDemandStore.count);
    doc["alkDemandLearning"]["ready"] = alkDemandStore.count >= 7;
    doc["alkDemandLearning"]["recommendedDemandDkhDay"] = alkDemandStore.recommendedDailyDemandDkh;
    doc["alkDemandLearning"]["recommendedP4MlDay"] = alkDemandStore.lastRecommendedP4MlDay;
    doc["alkDemandLearning"]["currentP4MlDay"] = baselineMgMlDay;
    doc["calciumDemandLearning"]["enabled"] = automaticCalciumLearningEnabled;
    doc["calciumDemandLearning"]["daysCollected"] = (int)calciumDemandStore.count;
    doc["calciumDemandLearning"]["ready"] = calciumDemandStore.count >= 7;
    doc["calciumDemandLearning"]["recommendedDemandPpmDay"] = calciumDemandStore.recommendedDailyDemandPpm;
    doc["calciumDemandLearning"]["recommendedP2MlDay"] = calciumDemandStore.lastRecommendedP2MlDay;

    // Added 2026-08-04, §4.2/§4.3: real Week/Month trend status, unlike
    // the legacy alkDemandLearning/calciumDemandLearning sections above
    // (confirmed dead for v2 earlier tonight -- kept only because removing
    // them risked breaking something else this session, see main.cpp's
    // own comment on that). This is the genuine, currently-working
    // replacement. hasWeekData/hasMonthData gate whether trendPerDay is
    // meaningful yet -- a device with only hours of real history
    // correctly reports false/0, not a misleadingly-early number.
    {
        const char* paramKeys[kNumParams] = {"alk", "ph", "ca", "mg"};
        for (int p = 0; p < kNumParams; p++) {
            if (p == P_PH) continue; // never ingested into a filter
            WaterParam wp = (WaterParam)p;
            const char* key = paramKeys[p];
            doc["weekMonthTrend"][key]["hasWeekData"] = ai.hasWeekData(wp);
            doc["weekMonthTrend"][key]["weekTrendPerDay"] = ai.hasWeekData(wp) ? ai.getWeekTrend(wp) : 0.0f;
            doc["weekMonthTrend"][key]["hasMonthData"] = ai.hasMonthData(wp);
            doc["weekMonthTrend"][key]["monthTrendPerDay"] = ai.hasMonthData(wp) ? ai.getMonthTrend(wp) : 0.0f;
            doc["weekMonthTrend"][key]["isDrifting"] = ai.isDrifting(wp);
            doc["weekMonthTrend"][key]["wasAnomaly"] = ai.wasAnomaly(wp);
            doc["weekMonthTrend"][key]["historySpanDays"] = ai.historySpanDays(wp);
        }
    }
    doc["calciumDemandLearning"]["currentP2MlDay"] = baselineCacl2MlDay;
    doc["chemicalStrengths"]["kalk"] = kalkStrengthDkhPerMl;
    doc["chemicalStrengths"]["afr"] = afrStrengthDkhPerMl;
    doc["chemicalStrengths"]["alk"] = alkStrengthDkhPerMl;
    doc["chemicalStrengths"]["naoh"] = naohStrengthDkhPerMl;
    doc["chemicalStrengths"]["mg"] = mgStrengthPpmPerMl;
    doc["chemicalStrengths"]["cacl2"] = cacl2StrengthPpmPerMl;

    doc["chemicalRecipes"]["kalkGpg"] = recipeKalkGpg;
    doc["chemicalRecipes"]["afrGpg"] = recipeAfrGpg;
    doc["chemicalRecipes"]["afrType"] = recipeAfrType;
    doc["chemicalRecipes"]["alkGpg"] = recipeAlkGpg;
    doc["chemicalRecipes"]["naohGpg"] = recipeNaohGpg;
    doc["chemicalRecipes"]["mgGpg"] = recipeMgGpg;
    doc["chemicalRecipes"]["cacl2Gpg"] = recipeCacl2Gpg;
    doc["chemicalRecipes"]["alkType"] = recipeAlkType;
    doc["chemicalRecipes"]["mgType"] = recipeMgType;
    doc["chemicalRecipes"]["cacl2Type"] = recipeCacl2Type;
    doc["dosingMlPerDay"]["kalk"] = currentPlan.kalk;
    doc["dosingMlPerDay"]["afr"] = currentPlan.afr;
    doc["dosingMlPerDay"]["alk"] = currentPlan.alk;
    doc["dosingMlPerDay"]["cacl2"] = currentPlan.cacl2;
    doc["dosingMlPerDay"]["naoh"] = currentPlan.naoh;
    doc["dosingMlPerDay"]["mg"] = currentPlan.mg;
    // §5 free chemical declaration: mode-agnostic mirror by physical pump
    // index, correct for any declared chemical (not just the six legacy
    // names above). Phase 2's dashboard reads this instead.
    {
        float pumpPlanMl[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int c = 0; c < declaredChemicalCount && c < kMaxDeclaredChemicals; c++) {
            int pumpIdx = declaredChemicals[c].pumpIndex;
            if (pumpIdx >= 0 && pumpIdx < 4) pumpPlanMl[pumpIdx] = planMlPerDayByIndex[c];
        }
        doc["dosingMlPerDayByPump"]["pump1"] = pumpPlanMl[0];
        doc["dosingMlPerDayByPump"]["pump2"] = pumpPlanMl[1];
        doc["dosingMlPerDayByPump"]["pump3"] = pumpPlanMl[2];
        doc["dosingMlPerDayByPump"]["pump4"] = pumpPlanMl[3];
    }

    // §1/§3.2 "reduce required manual testing frequency over time... actively
    // prompts if 7+ days have passed with no manual cross-check." Owner
    // decision 2026-07-24: applicable only to manual-only tanks (no Apex/
    // Trident auto-testing) -- see isManualOnlyTank()'s comment. `applicable`
    // lets the dashboard simply hide this card entirely for Apex-equipped
    // tanks rather than needing its own copy of that logic.
    {
        bool applicable = isManualOnlyTank();
        doc["manualTestPrompt"]["applicable"] = applicable;
        if (applicable) {
            int daysSince = daysSinceLastManualTest();
            int recommendedDays = recommendedManualTestIntervalDays();
            doc["manualTestPrompt"]["daysSinceLastTest"] = daysSince; // -1 = never recorded / clock not synced yet
            doc["manualTestPrompt"]["recommendedIntervalDays"] = recommendedDays;
            doc["manualTestPrompt"]["maturity"] = manualTestGoverningMaturity();
            doc["manualTestPrompt"]["needed"] = (daysSince < 0) || (daysSince >= recommendedDays);
        }
    }

    // §8.5 chemistry targets.
    doc["targetAlk"] = targetAlk;
    doc["targetCa"] = targetCa;
    doc["targetMg"] = targetMg;
    // Added 2026-08-05: was entirely absent from this response. Confirmed
    // during debugging that this absence was a real diagnostic signal, not
    // just cosmetic -- targetPhLow/targetPhHigh didn't exist anywhere in
    // main.cpp either, which meant pH correction silently never worked.
    // See main.cpp's targetPhLow/targetPhHigh declaration comment for the
    // full root-cause explanation.
    doc["targetPhLow"] = targetPhLow;
    doc["targetPhHigh"] = targetPhHigh;

    // §8/§8.5 one-time setup wizard.
    doc["shouldShowSetupWizard"] = shouldShowSetupWizard();

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

    doc["buckets"]["p1"] = pumpBuckets[0];
    doc["buckets"]["p2"] = pumpBuckets[1];
    doc["buckets"]["p3"] = pumpBuckets[2];
    doc["buckets"]["p4"] = pumpBuckets[3];
    doc["buckets"]["kalk"] = pumpBuckets[0];
    doc["buckets"]["cacl2"] = pumpBuckets[1];
    doc["buckets"]["naoh"] = pumpBuckets[2];
    if (dosingMode == 7) {
        doc["buckets"]["alk"] = pumpBuckets[3];
        doc["buckets"]["mg"] = 0.0f;
    } else if (dosingMode == 8) {
        doc["buckets"]["alk"] = 0.0f;
        doc["buckets"]["mg"] = 0.0f;
    } else {
        doc["buckets"]["alk"] = alkBucket;
        doc["buckets"]["mg"] = pumpBuckets[3];
    }
    doc["buckets"]["ca"] = pumpBuckets[1];

    doc["dosingThreshold"] = DOSING_THRESHOLD;
    doc["dosingThresholds"]["p1"] = getPumpDoseThresholdMl(0);
    doc["dosingThresholds"]["p2"] = getPumpDoseThresholdMl(1);
    doc["dosingThresholds"]["p3"] = getPumpDoseThresholdMl(2);
    doc["dosingThresholds"]["p4"] = getPumpDoseThresholdMl(3);
    doc["maxHourlyLimit"] = maxDoseLimit;
    for (int i = 0; i < 4; ++i) {
        String p = "p" + String(i + 1);
        doc["pumpSafeties"][p]["thresholdMl"] = getPumpDoseThresholdMl(i);
        doc["pumpSafeties"][p]["maxDoseMl"] = getPumpMaxDoseMl(i);
        doc["pumpSafeties"][p]["maxDayMl"] = getPumpMaxDayMl(i);
        doc["pumpSafeties"][p]["usedTodayMl"] = dailyDoseTotals[i];
        doc["pumpSafeties"][p]["remainingTodayMl"] = getPumpDailyRemainingMl(i);
    }

    doc["mode7DayNightSplit"]["enabled"] = mode7DayNightSplitEnabled;
    doc["mode7DayNightSplit"]["dayNaohPct"] = mode7DayNaohPct;
    doc["mode7DayNightSplit"]["dayAlkPct"] = mode7DayAlkPct;
    doc["mode7DayNightSplit"]["nightNaohPct"] = mode7NightNaohPct;
    doc["mode7DayNightSplit"]["nightAlkPct"] = mode7NightAlkPct;
    doc["mode7DayNightSplit"]["naohMaxPh"] = mode7NaohMaxPh;
    doc["mode7DayNightSplit"]["lightsActive"] = isLightsOn();

    doc["aiChemistrySafeties"]["maxKalkDayMl"] = aiMaxKalkDayMl;
    doc["aiChemistrySafeties"]["maxNaohDayMl"] = aiMaxNaohDayMl;
    doc["aiChemistrySafeties"]["maxAlkDayMl"] = aiMaxAlkDayMl;
    doc["aiChemistrySafeties"]["maxAlkRiseDkhDay"] = aiMaxAlkRiseDkhDay;
    // Added 2026-08-04: maxCaRisePpmDay genuinely feeds the live v2 safety
    // envelope (ai.safety.maxCaRisePerDayPpm) but was never exposed in this
    // status payload at all -- found while restoring dashboard UI for the
    // 3 fields mistakenly removed along with the genuinely-dead ones in
    // the "AI Chemistry Safeties" card cleanup.
    doc["aiChemistrySafeties"]["maxCaRisePpmDay"] = aiMaxCaRisePpmDay;
    doc["aiChemistrySafeties"]["maxMgCorrectionDayMl"] = aiMaxMgCorrectionDayMl;
    doc["aiChemistrySafeties"]["maxMgDayMl"] = aiMaxMgDayMl;
    doc["aiChemistrySafeties"]["mgDeadbandPpm"] = aiMgDeadbandPpm;

    for (int i = 0; i < 4; ++i) {
        String pump = "p" + String(i + 1);
        JsonObject reservoir = doc["chemicalLevels"][pump].to<JsonObject>();
        reservoir["capacityGal"] = chemicalCapacityGal[i];
        reservoir["remainingMl"] = chemicalRemainingMl[i];
        reservoir["remainingGal"] = chemicalRemainingMl[i] / ML_PER_GALLON;
        reservoir["remainingPct"] = (chemicalCapacityGal[i] > 0.0f)
            ? (chemicalRemainingMl[i] / (chemicalCapacityGal[i] * ML_PER_GALLON)) * 100.0f
            : 0.0f;
        reservoir["enabled"] = chemicalCapacityGal[i] > 0.0f;
        reservoir["warning"] = chemicalCapacityGal[i] > 0.0f && chemicalRemainingMl[i] <= ML_PER_GALLON;
        reservoir["severe"] = chemicalCapacityGal[i] > 0.0f && chemicalRemainingMl[i] <= (0.5f * ML_PER_GALLON);
    }
    
    unsigned long __t1 = millis();
    String response;
    serializeJson(doc, response);
    unsigned long __t2 = millis();
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.send(200, "application/json", response);
    unsigned long __t3 = millis();
    Serial.printf("STATUS TIMING: build=%lums serialize=%lums send=%lums total=%lums bytes=%u\n",
                  __t1 - __t0, __t2 - __t1, __t3 - __t2, __t3 - __t0, response.length());
    logger.printf("STATUS TIMING: build=%lums serialize=%lums send=%lums total=%lums bytes=%u\n",
                  __t1 - __t0, __t2 - __t1, __t3 - __t2, __t3 - __t0, response.length());
}

// NOTE: handlePostVolume was defined in main.cpp but never registered
// with server.on(...) anywhere - it looks like dead code left over from
// before /api/config/volume's current inline-lambda handler existed.
// Kept here, still unused, so behavior is unchanged. Safe to delete once
// confirmed nothing external depends on it.
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
    ai.tank.tankVolumeLiters = TANK_VOLUME_L; // v1 -> v2: direct field assignment, no gallons setter in v2

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

    // §5/Phase 1-3 transition compatibility shim: the OLD dashboard's mode
    // picker still works during the transition by regenerating the NEW
    // declared-chemical list to match whatever mode was just selected --
    // see buildDeclaredChemicalsFromLegacyMode()'s comment. This keeps
    // both dashboards consistent with whichever was used most recently.
    // Remove this call in Phase 3 once /api/dosing-mode itself is retired.
    buildDeclaredChemicalsFromLegacyMode(dosingMode);
    rebuildAiChemicalDeclarations();

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
    if (otaInProgress) {
        doser.stopAllPumps();
        server.send(423, "application/json", "{\"ok\":false,\"error\":\"OTA in progress; calibration blocked\"}");
        return;
    }

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

    // Fixed 2026-07-25: was bounded by pumpCountForCurrentDosingMode(), a
    // legacy Mode-based limit left over from before free chemical
    // declaration existed. A device that's never had dosingMode explicitly
    // set (e.g. mid-setup-wizard, before any legacy migration ran) could
    // silently reject calibration on pump 3/4 even with a real declared
    // chemical there. Any of the 4 physical pumps is valid under V2.
    if (pumpIndex < 0 || pumpIndex > 3 || seconds <= 0.0f || seconds > 300.0f) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid pumpIndex or seconds\"}");
        return;
    }

    doser.stopManualRun(pumpIndex);
    doser.startManualRun(pumpIndex);
    calibrationRunActive[pumpIndex] = true;
    calibrationRunUntilMs[pumpIndex] = millis() + (unsigned long)(seconds * 1000.0f);
    armPumpRuntimeDeadlineMs(
        pumpIndex,
        (unsigned long)(seconds * 1000.0f),
        "Calibration"
    );

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

void handlePostLiveDose() {
    // Fixed 2026-08-04: EVERY rejection path below previously returned an
    // HTTP error with zero Serial/logger output -- only the success path
    // was ever logged. Confirmed root cause of repeated "Quick Dose isn't
    // working, no logs" reports: the log genuinely could not distinguish
    // between OTA-blocked, missing body, emergency stop, another pump
    // running, or the inter-pump delay -- all five looked identical
    // (nothing) from the log alone, forcing every diagnosis attempt through
    // the browser's Network tab instead. Every early return below now logs
    // exactly why, so the actual reason is visible without needing devtools.
    if (otaInProgress) {
        doser.stopAllPumps();
        Serial.println("LIVE DOSE REJECTED: OTA in progress.");
        logger.println("LIVE DOSE REJECTED: OTA in progress.");
        server.send(423, "application/json", "{\"ok\":false,\"error\":\"OTA in progress; live dose blocked\"}");
        return;
    }

    if (!server.hasArg("plain")) {
        Serial.println("LIVE DOSE REJECTED: missing JSON body.");
        logger.println("LIVE DOSE REJECTED: missing JSON body.");
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }
    if (emergencyStop) {
        Serial.println("LIVE DOSE REJECTED: emergency stop is active.");
        logger.println("LIVE DOSE REJECTED: emergency stop is active.");
        server.send(423, "application/json", "{\"ok\":false,\"error\":\"Emergency stop is active\"}");
        return;
    }

    if (anyDoserPumpRunning()) {
        Serial.println("LIVE DOSE REJECTED: another pump is currently dosing.");
        logger.println("LIVE DOSE REJECTED: another pump is currently dosing.");
        server.send(423, "application/json", "{\"ok\":false,\"error\":\"Another pump is currently dosing\"}");
        return;
    }

    // Removed 2026-08-04, at owner's explicit direction: Quick Dose (a
    // single, deliberate, operator-supervised manual action) no longer
    // has to wait out the inter-pump delay before firing -- the automated
    // scheduler ([AutoDose]) still fully respects it, since that check is
    // unchanged in the scheduler's own bucket-service loop. The real
    // safety reasoning behind the delay (giving a chemical time to
    // disperse before another one goes in) is still honored going
    // FORWARD from a Quick Dose: noteInterPumpDoseStarted() below still
    // arms the same delay after this dose fires, so whatever comes next
    // (another Quick Dose, or the automatic scheduler) still has to wait.
    // Only the "wait before THIS dose is allowed to start" requirement is
    // removed, specifically for this manually-triggered path.

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        Serial.printf("LIVE DOSE REJECTED: invalid JSON (%s).\n", error.c_str());
        logger.printf("LIVE DOSE REJECTED: invalid JSON (%s).\n", error.c_str());
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    int pumpIndex = doc["pumpIndex"] | (doc["pump"] | -1);
    float ml = doc["ml"] | 0.0f;

    // Fixed 2026-08-04: same bug as the pump calibration handler above
    // (see that fix's 2026-07-25 comment) -- was bounded by
    // pumpCountForCurrentDosingMode(), a legacy Mode-based limit that
    // returns as few as 1 for a stale/default dosingMode value. This is
    // the confirmed root cause of "Quick Dose only works for Pump 1" --
    // any pumpIndex >= 1 was being silently rejected here regardless of
    // whether a real chemical was declared on that pump. Any of the 4
    // physical pumps is valid under V2's free chemical declaration; this
    // handler just never received the fix already applied to calibration.
    if (pumpIndex < 0 || pumpIndex > 3 || ml <= 0.0f) {
        Serial.printf("LIVE DOSE REJECTED: invalid pumpIndex=%d or ml=%.2f.\n", pumpIndex, ml);
        logger.printf("LIVE DOSE REJECTED: invalid pumpIndex=%d or ml=%.2f.\n", pumpIndex, ml);
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid pumpIndex or ml\"}");
        return;
    }

    float safeMl = applyPumpSafetyCaps(pumpIndex, ml, "LiveDose");
    if (safeMl <= 0.0f) {
        float maxDose = getPumpMaxDoseMl(pumpIndex);
        float dailyRemaining = getPumpDailyRemainingMl(pumpIndex);
        Serial.printf("LIVE DOSE REJECTED: safety caps reduced pump %d's %.2f mL request to 0 "
                      "(maxSingleDose=%.2f, dailyRemainingMl=%.2f, usedTodayMl=%.2f).\n",
                      pumpIndex + 1, ml, maxDose, dailyRemaining, dailyDoseTotals[pumpIndex]);
        logger.printf("LIVE DOSE REJECTED: safety caps reduced pump %d's %.2f mL request to 0 "
                      "(maxSingleDose=%.2f, dailyRemainingMl=%.2f, usedTodayMl=%.2f).\n",
                      pumpIndex + 1, ml, maxDose, dailyRemaining, dailyDoseTotals[pumpIndex]);
        server.send(423, "application/json", "{\"ok\":false,\"error\":\"Pump safety blocked dose\"}");
        return;
    }

    // Arm the fail-safe before starting the pump so the runtime supervisor
    // can never observe a running pump without an active deadline.
    armPumpRuntimeDeadlineForMl(pumpIndex, safeMl, "LiveDose");

    float actualMl = doser.doseMl(pumpIndex, safeMl);
    if (actualMl <= 0.0f) {
        clearPumpRuntimeDeadline();
        Serial.printf("Local live dose failed to start: physical pump %d, requested %.2f ml, safety %.2f ml\n",
                      pumpIndex + 1, ml, safeMl);
        logger.printf("Local live dose failed to start: physical pump %d, requested %.2f ml, safety %.2f ml\n",
                      pumpIndex + 1, ml, safeMl);
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"Dose failed to start\"}");
        return;
    }

    noteInterPumpDoseStarted(pumpIndex, "LiveDose");
    recordChemicalDispense(pumpIndex, actualMl, "LiveDose");
    evaluateAlertState("ChemicalLevel", true);

    // Added 2026-08-04, at owner's direct observation: a manual Quick Dose
    // puts real chemical into the tank exactly like an AI-planned dose --
    // until now, AIEngineV2 had no idea this happened at all, so its
    // learning (in particular the confidence-learning feedback loop, see
    // AI_EngineV2.cpp) could misattribute this dose's real effect on the
    // water to whatever the AI itself happened to be dosing that cycle.
    // Feeds it into the exact same known-dose-effect mechanism a
    // AI-planned dose uses. Silently does nothing if this pump has no
    // matching active declared chemical (chemicalIndexForPump returns -1)
    // -- the physical dose still happened, it's just invisible to a model
    // that has nothing declared for this pump, same honest limitation as
    // any other unmodeled event.
    int chemForThisPump = chemicalIndexForPump(pumpIndex);
    if (chemForThisPump >= 0) {
        ai.recordManualDose(chemForThisPump, actualMl);
    }

    Serial.printf("Local live dose: physical pump %d, requested %.2f ml, safety %.2f ml, actual %.2f ml\n", pumpIndex + 1, ml, safeMl, actualMl);
    logger.printf("Local live dose: physical pump %d, requested %.2f ml, safety %.2f ml, actual %.2f ml\n", pumpIndex + 1, ml, safeMl, actualMl);
     server.send(200, "application/json", "{\"ok\":true}");
}

void handleGetChemicalLevels() {
    JsonDocument doc;
    doc["ok"] = true;
    for (int i = 0; i < 4; ++i) {
        String pump = "p" + String(i + 1);
        JsonObject reservoir = doc["chemicalLevels"][pump].to<JsonObject>();
        reservoir["capacityGal"] = chemicalCapacityGal[i];
        reservoir["remainingMl"] = chemicalRemainingMl[i];
        reservoir["remainingGal"] = chemicalRemainingMl[i] / ML_PER_GALLON;
        reservoir["remainingPct"] = (chemicalCapacityGal[i] > 0.0f)
            ? (chemicalRemainingMl[i] / (chemicalCapacityGal[i] * ML_PER_GALLON)) * 100.0f
            : 0.0f;
        reservoir["enabled"] = chemicalCapacityGal[i] > 0.0f;
        reservoir["warning"] = chemicalCapacityGal[i] > 0.0f && chemicalRemainingMl[i] <= ML_PER_GALLON;
        reservoir["severe"] = chemicalCapacityGal[i] > 0.0f && chemicalRemainingMl[i] <= (0.5f * ML_PER_GALLON);
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handlePostChemicalLevels() {
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

    // resetRemaining=true means "Fill to Full".
    // setCurrent=true means dashboard is sending actual current gallons for partial refills.
    bool resetRemaining = doc["resetRemaining"] | false;
    bool setCurrent = doc["setCurrent"] | false;

    for (int i = 0; i < 4; ++i) {
        String galKey = "p" + String(i + 1) + "Gal";
        String currentGalKey = "p" + String(i + 1) + "CurrentGal";
        String pumpKey = "p" + String(i + 1);

        float oldCapGal = chemicalCapacityGal[i];
        float gal = oldCapGal;

        if (doc[galKey].is<float>() || doc[galKey].is<int>()) {
            gal = doc[galKey] | oldCapGal;
        } else if (doc[pumpKey]["capacityGal"].is<float>() || doc[pumpKey]["capacityGal"].is<int>()) {
            gal = doc[pumpKey]["capacityGal"] | oldCapGal;
        }

        if (!isfinite(gal) || gal < 0.0f) gal = 0.0f;
        if (gal > 20.0f) gal = 20.0f; // sanity limit

        chemicalCapacityGal[i] = gal;
        float maxMl = gal * ML_PER_GALLON;

        if (gal <= 0.0f) {
            chemicalRemainingMl[i] = 0.0f;
            continue;
        }

        if (resetRemaining) {
            // Fill to Full
            chemicalRemainingMl[i] = maxMl;
        } else if (setCurrent) {
            // Partial refill / manually set actual current amount.
            // Supports either top-level p1CurrentGal or nested p1.currentGal.
            bool hasCurrent = false;
            float currentGal = chemicalRemainingMl[i] / ML_PER_GALLON;

            if (doc[currentGalKey].is<float>() || doc[currentGalKey].is<int>()) {
                currentGal = doc[currentGalKey] | currentGal;
                hasCurrent = true;
            } else if (doc[pumpKey]["currentGal"].is<float>() || doc[pumpKey]["currentGal"].is<int>()) {
                currentGal = doc[pumpKey]["currentGal"] | currentGal;
                hasCurrent = true;
            } else if (doc[pumpKey]["remainingGal"].is<float>() || doc[pumpKey]["remainingGal"].is<int>()) {
                currentGal = doc[pumpKey]["remainingGal"] | currentGal;
                hasCurrent = true;
            }

            if (hasCurrent) {
                if (!isfinite(currentGal) || currentGal < 0.0f) currentGal = 0.0f;
                if (currentGal > gal) currentGal = gal;
                chemicalRemainingMl[i] = currentGal * ML_PER_GALLON;
            } else {
                if (chemicalRemainingMl[i] > maxMl) chemicalRemainingMl[i] = maxMl;
                if (chemicalRemainingMl[i] < 0.0f) chemicalRemainingMl[i] = 0.0f;
            }
        } else {
            // Save Capacity only: never assume refill.
            // Preserve actual remaining amount, clamped to new capacity.
            if (chemicalRemainingMl[i] > maxMl) chemicalRemainingMl[i] = maxMl;
            if (chemicalRemainingMl[i] < 0.0f) chemicalRemainingMl[i] = 0.0f;
        }
    }

    saveChemicalReservoirs();
    evaluateAlertState("ChemicalLevel", true);

    Serial.println("Chemical reservoir levels saved locally.");
    logger.println("Chemical reservoir levels saved locally.");

    handleGetChemicalLevels();
}

void handlePostEmergencyStop() {
    if (!emergencyStop) {
        triggerEmergencyStop("Emergency Stop button pressed on local dashboard", "LocalDashboard");
    } else {
        emergencyStop = false;
        emergencyStopReason = "";
        clearPumpRuntimeDeadline();
        Serial.println("EMERGENCY STOP CLEARED [LocalDashboard]");
        logger.println("EMERGENCY STOP CLEARED [LocalDashboard]");

        if (WiFi.status() == WL_CONNECTED) {
            Firebase.setBool(writeFbdo, ("/devices/" + deviceID + "/state/emergencyStop").c_str(), false);
            Firebase.setString(writeFbdo, ("/devices/" + deviceID + "/state/emergencyStopReason").c_str(), "Cleared from local dashboard");
        }
        publishAlertState("info", "EMERGENCY_STOP", "Emergency Stop cleared from local dashboard", false, "LocalDashboard", true);
    }

    server.send(200, "application/json", "{\"ok\":true}");
}

void handleGetNotificationSettings() {
    JsonDocument doc;
    doc["ok"] = true;
    doc["notificationLevel"] = notificationLevel;
    doc["alertState"]["active"] = currentAlertActive;
    doc["alertState"]["level"] = currentAlertLevel;
    doc["alertState"]["code"] = currentAlertCode;
    doc["alertState"]["message"] = currentAlertMessage;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handlePostNotificationSettings() {
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

    String requested = doc["notificationLevel"] | notificationLevel;
    if (!isValidNotificationLevel(requested)) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid notificationLevel\"}");
        return;
    }

    notificationLevel = requested;
    prefs.begin("doser-settings", false);
    prefs.putString("notif_level", notificationLevel);
    prefs.end();

    if (WiFi.status() == WL_CONNECTED && firebaseStarted && Firebase.ready()) {
        Firebase.setString(writeFbdo, ("/devices/" + deviceID + "/settings/notificationLevel").c_str(), notificationLevel);
    }

    Serial.println("Notification level changed to: " + notificationLevel);
    logger.println("Notification level changed to: " + notificationLevel);

    JsonDocument out;
    out["ok"] = true;
    out["notificationLevel"] = notificationLevel;
    String response;
    serializeJson(out, response);
    server.send(200, "application/json", response);
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

void handlePostMode7DayNightSplit() {
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

    bool enabled = doc["enabled"] | mode7DayNightSplitEnabled;
    float dayNaoh = sanitizePercent(doc["dayNaohPct"] | mode7DayNaohPct, mode7DayNaohPct);
    float dayAlk = sanitizePercent(doc["dayAlkPct"] | mode7DayAlkPct, mode7DayAlkPct);
    float nightNaoh = sanitizePercent(doc["nightNaohPct"] | mode7NightNaohPct, mode7NightNaohPct);
    float nightAlk = sanitizePercent(doc["nightAlkPct"] | mode7NightAlkPct, mode7NightAlkPct);
    float naohMaxPh = sanitizePhCutoff(doc["naohMaxPh"] | mode7NaohMaxPh, mode7NaohMaxPh);

    if ((dayNaoh + dayAlk) <= 0.0f || (nightNaoh + nightAlk) <= 0.0f) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Day and night split totals must be greater than zero\"}");
        return;
    }

    mode7DayNightSplitEnabled = enabled;
    mode7DayNaohPct = dayNaoh;
    mode7DayAlkPct = dayAlk;
    mode7NightNaohPct = nightNaoh;
    mode7NightAlkPct = nightAlk;
    mode7NaohMaxPh = naohMaxPh;

    saveMode7DayNightSplit();
    applyMode7DayNightSplitToEngine();

    Serial.printf("Mode7 Day/Night Split Updated: enabled=%d day NaOH=%.1f Alk=%.1f night NaOH=%.1f Alk=%.1f NaOH cutoff=%.2f\n",
                  mode7DayNightSplitEnabled ? 1 : 0,
                  mode7DayNaohPct, mode7DayAlkPct,
                  mode7NightNaohPct, mode7NightAlkPct,
                  mode7NaohMaxPh);
    logger.printf("Mode7 Day/Night Split Updated: enabled=%d day NaOH=%.1f Alk=%.1f night NaOH=%.1f Alk=%.1f NaOH cutoff=%.2f\n",
                  mode7DayNightSplitEnabled ? 1 : 0,
                  mode7DayNaohPct, mode7DayAlkPct,
                  mode7NightNaohPct, mode7NightAlkPct,
                  mode7NaohMaxPh);

    calculateAiFromBestChemistry("Mode7SplitConfig");
    publishAiPlanIfNeeded("Mode7SplitConfig", true);

    server.send(200, "application/json", "{\"ok\":true}");
}

void handlePostAiChemistrySafeties() {
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

    float maxKalk = doc["maxKalkDayMl"] | aiMaxKalkDayMl;
    float maxNaoh = doc["maxNaohDayMl"] | aiMaxNaohDayMl;
    float maxAlk = doc["maxAlkDayMl"] | aiMaxAlkDayMl;
    float maxAlkRise = doc["maxAlkRiseDkhDay"] | aiMaxAlkRiseDkhDay;
    float maxCaRise = doc["maxCaRisePpmDay"] | aiMaxCaRisePpmDay;
    float maxMgCorr = doc["maxMgCorrectionDayMl"] | aiMaxMgCorrectionDayMl;
    float maxMg = doc["maxMgDayMl"] | aiMaxMgDayMl;
    float mgDeadband = doc["mgDeadbandPpm"] | aiMgDeadbandPpm;

    if (!isfinite(maxKalk) || maxKalk <= 0.0f || maxKalk > 250000.0f ||
        !isfinite(maxNaoh) || maxNaoh <= 0.0f || maxNaoh > 250000.0f ||
        !isfinite(maxAlk) || maxAlk <= 0.0f || maxAlk > 250000.0f ||
        !isfinite(maxAlkRise) || maxAlkRise <= 0.0f || maxAlkRise > 5.0f ||
        !isfinite(maxCaRise) || maxCaRise <= 0.0f || maxCaRise > 50.0f ||
        !isfinite(maxMgCorr) || maxMgCorr < 0.0f || maxMgCorr > 250000.0f ||
        !isfinite(maxMg) || maxMg <= 0.0f || maxMg > 250000.0f ||
        !isfinite(mgDeadband) || mgDeadband < 0.0f || mgDeadband > 200.0f) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid AI chemistry safety values\"}");
        return;
    }

    aiMaxKalkDayMl = maxKalk;
    aiMaxNaohDayMl = maxNaoh;
    aiMaxAlkDayMl = maxAlk;
    aiMaxAlkRiseDkhDay = maxAlkRise;
    aiMaxCaRisePpmDay = maxCaRise;
    aiMaxMgCorrectionDayMl = maxMgCorr;
    aiMaxMgDayMl = maxMg;
    aiMgDeadbandPpm = mgDeadband;

    saveAiChemistrySafeties();
    applyAiChemistrySafetiesToEngine();

    Serial.printf("AI Chemistry Safeties Updated: maxKalk=%.2f maxNaOH=%.2f maxAlk=%.2f maxAlkRise=%.2f maxMgCorr=%.2f maxMg=%.2f mgDeadband=%.2f\n",
                  aiMaxKalkDayMl, aiMaxNaohDayMl, aiMaxAlkDayMl, aiMaxAlkRiseDkhDay,
                  aiMaxMgCorrectionDayMl, aiMaxMgDayMl, aiMgDeadbandPpm);
    logger.printf("AI Chemistry Safeties Updated: maxKalk=%.2f maxNaOH=%.2f maxAlk=%.2f maxAlkRise=%.2f maxMgCorr=%.2f maxMg=%.2f mgDeadband=%.2f\n",
                  aiMaxKalkDayMl, aiMaxNaohDayMl, aiMaxAlkDayMl, aiMaxAlkRiseDkhDay,
                  aiMaxMgCorrectionDayMl, aiMaxMgDayMl, aiMgDeadbandPpm);

    calculateAiFromBestChemistry("AiChemSafetyConfig");
    publishAiPlanIfNeeded("AiChemSafetyConfig", true);

    server.send(200, "application/json", "{\"ok\":true}");
}

void handlePostDosingSafeties() {
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

    // Backward-compatible old dashboard support: threshold/maxLimit apply to all pumps.
    if (doc["threshold"].is<float>() || doc["threshold"].is<int>()) {
        float threshold = doc["threshold"] | DOSING_THRESHOLD;
        if (!isfinite(threshold) || threshold <= 0.0f || threshold > 10000.0f) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid threshold\"}");
            return;
        }
        for (int i = 0; i < 4; ++i) pumpDoseThresholdMl[i] = threshold;
    }

    if (doc["maxLimit"].is<float>() || doc["maxLimit"].is<int>()) {
        float maxLimit = doc["maxLimit"] | maxDoseLimit;
        if (!isfinite(maxLimit) || maxLimit <= 0.0f || maxLimit > 100000.0f) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid max limit\"}");
            return;
        }
        for (int i = 0; i < 4; ++i) pumpMaxDoseMl[i] = maxLimit;
    }

    // New dashboard support: pumpSafeties.p1/p2/p3/p4 each has thresholdMl/maxDoseMl/maxDayMl.
    JsonObject safeties = doc["pumpSafeties"].as<JsonObject>();
    if (!safeties.isNull()) {
        for (int i = 0; i < 4; ++i) {
            String p = "p" + String(i + 1);
            JsonObject row = safeties[p].as<JsonObject>();
            if (row.isNull()) continue;

            float threshold = row["thresholdMl"] | pumpDoseThresholdMl[i];
            float maxDose = row["maxDoseMl"] | pumpMaxDoseMl[i];
            float maxDay = row["maxDayMl"] | pumpMaxDayMl[i];

            if (!isfinite(threshold) || threshold <= 0.0f || threshold > 10000.0f ||
                !isfinite(maxDose) || maxDose <= 0.0f || maxDose > 100000.0f ||
                !isfinite(maxDay) || maxDay <= 0.0f || maxDay > 250000.0f) {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid per-pump safety values\"}");
                return;
            }

            pumpDoseThresholdMl[i] = threshold;
            pumpMaxDoseMl[i] = maxDose;
            pumpMaxDayMl[i] = maxDay;
        }
    }

    // Keep global aliases on P1 for older UI/status fields.
    DOSING_THRESHOLD = pumpDoseThresholdMl[0];
    maxDoseLimit = pumpMaxDoseMl[0];
    savePumpSafeties();

    Serial.printf("Pump Safeties Updated: P1 thr=%.2f max=%.2f day=%.2f | P2 thr=%.2f max=%.2f day=%.2f | P3 thr=%.2f max=%.2f day=%.2f | P4 thr=%.2f max=%.2f day=%.2f\n",
                  pumpDoseThresholdMl[0], pumpMaxDoseMl[0], pumpMaxDayMl[0],
                  pumpDoseThresholdMl[1], pumpMaxDoseMl[1], pumpMaxDayMl[1],
                  pumpDoseThresholdMl[2], pumpMaxDoseMl[2], pumpMaxDayMl[2],
                  pumpDoseThresholdMl[3], pumpMaxDoseMl[3], pumpMaxDayMl[3]);
    logger.printf("Pump Safeties Updated: P1 thr=%.2f max=%.2f day=%.2f | P2 thr=%.2f max=%.2f day=%.2f | P3 thr=%.2f max=%.2f day=%.2f | P4 thr=%.2f max=%.2f day=%.2f\n",
                  pumpDoseThresholdMl[0], pumpMaxDoseMl[0], pumpMaxDayMl[0],
                  pumpDoseThresholdMl[1], pumpMaxDoseMl[1], pumpMaxDayMl[1],
                  pumpDoseThresholdMl[2], pumpMaxDoseMl[2], pumpMaxDayMl[2],
                  pumpDoseThresholdMl[3], pumpMaxDoseMl[3], pumpMaxDayMl[3]);

    server.send(200, "application/json", "{\"ok\":true}");
}

void handleRoot() {
    // Fixed 2026-08-05: was sending the raw 194KB HTML/CSS/JS string
    // uncompressed on every single page load. kIndexHtml is now a
    // pre-gzipped byte array (kIndexHtmlGz, generated from the exact same
    // content -- 194031 -> 47532 bytes, about a 75% reduction) rather than
    // a plain string; the browser transparently decompresses it as long
    // as Content-Encoding is sent BEFORE the body, which is why
    // sendHeader() comes first here.
    server.sendHeader(F("Content-Encoding"), F("gzip"));
    server.send_P(200, "text/html", (const char*)kIndexHtmlGz, kIndexHtmlGzLen);
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

    // Reject impossible or dangerously mistyped manual values before they can
    // enter learner history or change a dosing plan.
    if (!isfinite(alk) || alk < 4.0f || alk > 15.0f ||
        !isfinite(ca)  || ca  < 250.0f || ca > 700.0f ||
        !isfinite(mg)  || mg  < 800.0f || mg > 1800.0f ||
        !isfinite(ph)  || ph  < 6.50f || ph > 9.00f) {
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"Manual chemistry value outside safe validation range\"}");
        return;
    }

    currentAlk = alk;
    currentCa = ca;
    currentMg = mg;
    currentPh = ph;
    saveManualTestLocally(alk, ca, mg, ph);
    hasSavedManualTest = true;

    // Every explicit manual submission is a new physical chemistry event.
    // It therefore advances the same learner pipeline used by Apex/Trident.
    acceptNewChemistryMeasurement("Manual", "", true);
    calculateAiFromBestChemistry("Manual", true);
    addCurrentAiPlanToBuckets("Manual", true);
    publishAiPlanIfNeeded("Manual", true);

    // Manual entry is already the new source of truth. Publish it immediately;
    // do not call syncAllTruths() here because a live Apex poll can overwrite
    // the just-entered values before either dashboard displays them.
    bool firebaseUpdated = mirrorStatusToFirebase();
    server.send(200, "application/json", firebaseUpdated
        ? "{\"ok\":true,\"learningAccepted\":true,\"firebaseUpdated\":true}"
        : "{\"ok\":true,\"learningAccepted\":true,\"firebaseUpdated\":false}");
}

// =============================================================================
// §5 free chemical declaration -- replaces the mode picker. See
// DASHBOARD_MIGRATION_PLAN.md for the full design. These five handlers are
// the entire new API surface: list, add, edit, remove, and list available
// presets (so the dashboard's preset dropdown has exactly ONE source of
// truth -- this endpoint reading ChemicalPresets.h's table -- instead of a
// second hand-copied version in JS, which is exactly the problem
// DOSING_MODES already had).
// =============================================================================

void handleGetChemicals() {
    JsonDocument doc;
    doc["ok"] = true;
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
        o["nightFraction"] = d.nightFraction;
        o["daytimeSuppressPercent"] = d.daytimeSuppressPercent;
    }
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void handleGetChemicalPresets() {
    JsonDocument doc;
    doc["ok"] = true;
    JsonArray arr = doc["presets"].to<JsonArray>();
    for (int i = 0; i < (int)ChemicalPresetId::kNumPresets; i++) {
        const ChemicalPresetSpec& spec = kChemicalPresets[i];
        JsonObject o = arr.add<JsonObject>();
        o["id"] = i;
        o["name"] = spec.displayName;
        o["movesAlk"] = spec.potencyPerMlPerGallon[P_ALK] != 0.0f;
        o["movesCa"]  = spec.potencyPerMlPerGallon[P_CA]  != 0.0f;
        o["movesMg"]  = spec.potencyPerMlPerGallon[P_MG]  != 0.0f;
        o["phSensitive"] = spec.phSensitive;
        // Raw per-gallon constants (pre tank-volume-division) -- needed by
        // the setup wizard's DIY recipe scaling (§8.5: a customer mixing a
        // different grams-per-gallon than the standard recipe scales
        // potency proportionally from this reference, client-side, then
        // submits the result through the existing Custom-potency path).
        o["potencyAlkPerGallon"] = spec.potencyPerMlPerGallon[P_ALK];
        o["potencyCaPerGallon"]  = spec.potencyPerMlPerGallon[P_CA];
        o["potencyMgPerGallon"]  = spec.potencyPerMlPerGallon[P_MG];
        // Added 2026-08-04: confirmed missing -- the grams-per-gallon
        // recipe scaling path (savePotencyFromRecipe in Dashboard.h) had
        // no way to include pH even after real pH values were added to
        // every preset entry, because this endpoint never sent it at
        // all. NaOH and the new soda-ash preset both gained real pH
        // values tonight that were silently unreachable through this
        // specific path until now.
        o["potencyPhPerGallon"]  = spec.potencyPerMlPerGallon[P_PH];
    }
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void handlePostAddChemical() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    if (declaredChemicalCount >= kMaxDeclaredChemicals) {
        server.send(400, "application/json",
            "{\"ok\":false,\"error\":\"Maximum of 4 chemicals reached (one per physical pump)\"}");
        return;
    }

    String name = doc["name"] | "";
    int pumpIndex = doc["pumpIndex"] | -1;
    int presetId = doc["presetId"] | -1;

    if (name.length() == 0) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Name is required\"}");
        return;
    }
    if (pumpIndex < 0 || pumpIndex > 3) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"pumpIndex must be 0-3\"}");
        return;
    }
    // Owner decision 2026-07-24: always require a pump at creation, and a
    // physical pump can only run one chemical -- so pumpIndex must be
    // unique among declared chemicals.
    if (isPumpIndexTaken(pumpIndex, "")) {
        server.send(409, "application/json",
            "{\"ok\":false,\"error\":\"That pump already has a chemical assigned. Remove or reassign it first.\"}");
        return;
    }

    DeclaredChemical chem;
    chem.id = generateChemicalId();
    chem.name = name;
    chem.pumpIndex = pumpIndex;
    chem.active = true;

    if (presetId >= 0 && presetId < (int)ChemicalPresetId::kNumPresets) {
        chem.presetId = presetId;
        float tankVolumeGal = TANK_VOLUME_L / 3.78541f;
        ChemicalDeclaration fromPreset = presetToDeclaration((ChemicalPresetId)presetId, tankVolumeGal, name.c_str());
        chem.potencyAlkPerMl = fromPreset.potencyPerMl[P_ALK];
        chem.potencyCaPerMl  = fromPreset.potencyPerMl[P_CA];
        chem.potencyMgPerMl  = fromPreset.potencyPerMl[P_MG];
        chem.potencyPhPerMl  = fromPreset.potencyPerMl[P_PH];
        chem.phSensitive     = fromPreset.phSensitive;
        // Generalized 2026-07-27 (see DeclaredChemical::nightFraction):
        // Kalkwasser defaults to V1's current universal built-in split
        // (75% night / 25% day) for this specific chemical, not an
        // Eric-only value -- V1 has always applied this to every Mode 7
        // customer's Kalk pump, this just carries the same default
        // forward. Any other preset/custom chemical stays at the
        // DeclaredChemical default (50, flat/even) unless explicitly set
        // below.
        if (presetId == (int)ChemicalPresetId::DIY_Kalkwasser) chem.nightFraction = 75.0f;
    } else {
        // Added 2026-08-04, at owner's direct, specific finding: kalkwasser
        // is a SATURATED solution -- there is one real-world concentration
        // (bounded by how much calcium hydroxide will actually dissolve in
        // water), not a customer-varied recipe the way NaOH/CaCl2 are.
        // Confirmed root cause of a real 3,000x potency error on a live
        // customer device: kalkwasser was declared via this raw-number
        // path, and the number entered (0.014) turned out to be main.cpp's
        // generic fallback constant, not this tank's real strength -- with
        // nothing anywhere to catch it. Since kalk has no real "recipe" to
        // vary, there is no legitimate reason for it to ever go through
        // free-text entry -- force the fixed, chemistry-sourced preset
        // instead. Name-matched rather than gated by presetId alone, since
        // presetId is exactly the field this guard exists to prevent being
        // bypassed on.
        String lowerName = name;
        lowerName.toLowerCase();
        if (lowerName.indexOf("kalk") >= 0) {
            server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"Kalkwasser is a saturated solution with one real concentration -- "
                "use the Kalkwasser preset instead of a custom potency value.\"}");
            return;
        }

        chem.presetId = -1; // custom
        chem.potencyAlkPerMl = doc["potencyAlkPerMl"] | 0.0f;
        chem.potencyCaPerMl  = doc["potencyCaPerMl"]  | 0.0f;
        chem.potencyMgPerMl  = doc["potencyMgPerMl"]  | 0.0f;
        // Added 2026-08-04: optional, defaults to 0.0f (honest "not
        // declared" default, not "genuinely zero pH effect" -- see
        // DeclaredChemical::potencyPhPerMl's comment). Deliberately NOT
        // included in the sufficiency check below -- a chemical with a
        // real Alk/Ca/Mg contribution but undeclared pH effect is still a
        // perfectly valid, usable declaration; pH potency is a refinement,
        // not a requirement, same as phSensitive already being optional.
        chem.potencyPhPerMl  = doc["potencyPhPerMl"]  | 0.0f;
        chem.phSensitive     = doc["phSensitive"]     | false;

        if (chem.potencyAlkPerMl == 0.0f && chem.potencyCaPerMl == 0.0f && chem.potencyMgPerMl == 0.0f) {
            server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"Custom chemical needs at least one nonzero potency (Alk/Ca/Mg per mL)\"}");
            return;
        }

        // Added 2026-08-04: direct sanity check against main.cpp's known
        // generic fallback constants -- catches exactly the failure that
        // happened on a live device (a placeholder/default number, never
        // corrected to the customer's real value, sailing through with no
        // warning at all). A submitted value landing suspiciously close to
        // a generic default doesn't PROVE it's wrong, but it's worth a
        // deliberate confirmation rather than silent acceptance.
        auto suspiciouslyGeneric = [](float submitted, float genericDefault) {
            if (genericDefault == 0.0f) return false;
            return fabsf(submitted - genericDefault) < (0.001f * genericDefault);
        };
        bool matchesGenericDefault =
            suspiciouslyGeneric(chem.potencyAlkPerMl, kalkStrengthDkhPerMl) ||
            suspiciouslyGeneric(chem.potencyAlkPerMl, naohStrengthDkhPerMl) ||
            suspiciouslyGeneric(chem.potencyAlkPerMl, alkStrengthDkhPerMl) ||
            suspiciouslyGeneric(chem.potencyCaPerMl, cacl2StrengthPpmPerMl) ||
            suspiciouslyGeneric(chem.potencyMgPerMl, mgStrengthPpmPerMl);
        if (matchesGenericDefault && !(doc["confirmGenericValue"] | false)) {
            server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"This value matches a generic placeholder, not a tank-specific "
                "measurement. If this chemical's real strength genuinely matches, resubmit with "
                "confirmGenericValue: true. Otherwise enter this tank's actual mixed strength.\"}");
            return;
        }
    }

    // Defaults to this pump's existing configured safety cap if not given
    // explicitly -- matches buildDeclaredChemicalsFromLegacyMode()'s
    // migration behavior, same reasoning.
    chem.maxMlPerDay = doc["maxMlPerDay"] | pumpMaxDayMl[pumpIndex];
    if (!isfinite(chem.maxMlPerDay) || chem.maxMlPerDay < 0.0f) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid maxMlPerDay\"}");
        return;
    }

    // Added 2026-08-04: same migration-safe seeding pattern as maxMlPerDay
    // above, for the two fields that used to live ONLY on the pump slot
    // (see DeclaredChemical::bucketThresholdMl's comment for the full
    // reasoning). Using the getters (not the raw arrays directly) means a
    // freshly-declared chemical inherits whatever this pump's CURRENT
    // effective value is, even if another chemical was already declared
    // on this same pump earlier and already customized these fields.
    chem.bucketThresholdMl = doc["bucketThresholdMl"] | getPumpDoseThresholdMl(pumpIndex);
    if (!isfinite(chem.bucketThresholdMl) || chem.bucketThresholdMl <= 0.0f) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid bucketThresholdMl\"}");
        return;
    }
    chem.maxSingleDoseMl = doc["maxSingleDoseMl"] | getPumpMaxDoseMl(pumpIndex);
    if (!isfinite(chem.maxSingleDoseMl) || chem.maxSingleDoseMl <= 0.0f) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid maxSingleDoseMl\"}");
        return;
    }

    // Explicit override, if given, wins over whatever preset default was
    // just set above -- needed to carry forward an already-known-good
    // value (e.g. a customer's specific tuned split from a prior system)
    // rather than only ever getting the generic preset default.
    if (!doc["nightFraction"].isNull()) {
        float nf = doc["nightFraction"] | 50.0f;
        if (!isfinite(nf) || nf < 0.0f || nf > 100.0f) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"nightFraction must be 0-100\"}");
            return;
        }
        chem.nightFraction = nf;
    }

    if (!doc["daytimeSuppressPercent"].isNull()) {
        float dsp = doc["daytimeSuppressPercent"] | 0.0f;
        if (!isfinite(dsp) || dsp < 0.0f || dsp > 100.0f) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"daytimeSuppressPercent must be 0-100\"}");
            return;
        }
        chem.daytimeSuppressPercent = dsp;
    }

    declaredChemicals[declaredChemicalCount++] = chem;
    // Fixed 2026-08-02: saveDeclaredChemicals()'s bool return was discarded
    // here -- confirmed root cause of reefDoser12 appearing to accept a
    // maxMlPerDay edit (in-memory value was genuinely correct, dashboard
    // correctly showed it) that never actually became durable on disk. A
    // subsequent reboot (OTA or otherwise) then silently reloaded the last
    // value that WAS successfully persisted, with zero indication anything
    // had failed. rebuildAiChemicalDeclarations() still runs either way so
    // the in-memory/dosing state stays consistent with what was JUST added
    // for the rest of this boot -- only the client-visible success/failure
    // signal changes here, not the in-memory behavior.
    bool saved = saveDeclaredChemicals();
    rebuildAiChemicalDeclarations();

    if (!saved) {
        declaredChemicalCount--; // don't leave an unsaved entry silently active
        rebuildAiChemicalDeclarations();
        server.send(500, "application/json",
            "{\"ok\":false,\"error\":\"Chemical could not be saved to storage. Not added -- try again.\"}");
        return;
    }

    Serial.printf("CHEMICAL ADDED: %s on pump %d\n", chem.name.c_str(), pumpIndex + 1);
    logger.printf("CHEMICAL ADDED: %s on pump %d\n", chem.name.c_str(), pumpIndex + 1);

    calculateAiFromBestChemistry("ChemicalAdd");
    publishAiPlanIfNeeded("ChemicalAdd", true);

    JsonDocument out;
    out["ok"] = true;
    out["id"] = chem.id;
    String output;
    serializeJson(out, output);
    server.send(200, "application/json", output);
}

void handlePostUpdateChemical() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    String id = doc["id"] | "";
    int idx = findDeclaredChemicalIndexById(id);
    if (idx < 0) {
        server.send(404, "application/json", "{\"ok\":false,\"error\":\"Chemical id not found\"}");
        return;
    }

    DeclaredChemical& chem = declaredChemicals[idx];

    if (!doc["name"].isNull()) {
        String newName = doc["name"] | chem.name;
        if (newName.length() > 0) chem.name = newName;
    }

    if (!doc["pumpIndex"].isNull()) {
        int newPump = doc["pumpIndex"] | chem.pumpIndex;
        if (newPump < 0 || newPump > 3) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"pumpIndex must be 0-3\"}");
            return;
        }
        if (isPumpIndexTaken(newPump, id)) {
            server.send(409, "application/json",
                "{\"ok\":false,\"error\":\"That pump already has a different chemical assigned.\"}");
            return;
        }
        chem.pumpIndex = newPump;
    }

    if (!doc["presetId"].isNull()) {
        int newPreset = doc["presetId"] | -1;
        chem.presetId = newPreset;
        if (newPreset >= 0 && newPreset < (int)ChemicalPresetId::kNumPresets) {
            float tankVolumeGal = TANK_VOLUME_L / 3.78541f;
            ChemicalDeclaration fromPreset = presetToDeclaration((ChemicalPresetId)newPreset, tankVolumeGal, chem.name.c_str());
            chem.potencyAlkPerMl = fromPreset.potencyPerMl[P_ALK];
            chem.potencyCaPerMl  = fromPreset.potencyPerMl[P_CA];
            chem.potencyMgPerMl  = fromPreset.potencyPerMl[P_MG];
            chem.potencyPhPerMl  = fromPreset.potencyPerMl[P_PH];
            chem.phSensitive     = fromPreset.phSensitive;
        }
    }
    // Custom potency edits only take effect when NOT tied to a preset --
    // matches presetToDeclaration()'s "preset owns these fields" model.
    if (chem.presetId < 0) {
        // Added 2026-08-04: same guard as handlePostAddChemical -- an
        // already-declared kalk chemical must not be editable into a
        // custom potency value either (e.g. correcting a typo could
        // accidentally introduce a NEW wrong number just as easily as the
        // original one). See that handler's comment for the full
        // reasoning.
        String lowerName = chem.name;
        lowerName.toLowerCase();
        bool touchingPotency = !doc["potencyAlkPerMl"].isNull() || !doc["potencyCaPerMl"].isNull();
        if (lowerName.indexOf("kalk") >= 0 && touchingPotency) {
            server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"Kalkwasser is a saturated solution with one real concentration -- "
                "switch this chemical to the Kalkwasser preset (presetId) instead of editing a custom potency value.\"}");
            return;
        }

        float newAlk = doc["potencyAlkPerMl"].isNull() ? chem.potencyAlkPerMl : (float)doc["potencyAlkPerMl"];
        float newCa  = doc["potencyCaPerMl"].isNull()  ? chem.potencyCaPerMl  : (float)doc["potencyCaPerMl"];
        float newMg  = doc["potencyMgPerMl"].isNull()  ? chem.potencyMgPerMl  : (float)doc["potencyMgPerMl"];

        // Added 2026-08-04: same generic-placeholder sanity check as
        // handlePostAddChemical -- see that handler's comment for the
        // full reasoning. Only checked when a potency field is actually
        // being changed, not on every unrelated edit (e.g. nightFraction).
        if (touchingPotency) {
            auto suspiciouslyGeneric = [](float submitted, float genericDefault) {
                if (genericDefault == 0.0f) return false;
                return fabsf(submitted - genericDefault) < (0.001f * genericDefault);
            };
            bool matchesGenericDefault =
                suspiciouslyGeneric(newAlk, kalkStrengthDkhPerMl) ||
                suspiciouslyGeneric(newAlk, naohStrengthDkhPerMl) ||
                suspiciouslyGeneric(newAlk, alkStrengthDkhPerMl) ||
                suspiciouslyGeneric(newCa, cacl2StrengthPpmPerMl) ||
                suspiciouslyGeneric(newMg, mgStrengthPpmPerMl);
            if (matchesGenericDefault && !(doc["confirmGenericValue"] | false)) {
                server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"This value matches a generic placeholder, not a tank-specific "
                    "measurement. If this chemical's real strength genuinely matches, resubmit with "
                    "confirmGenericValue: true. Otherwise enter this tank's actual mixed strength.\"}");
                return;
            }
        }

        chem.potencyAlkPerMl = newAlk;
        chem.potencyCaPerMl  = newCa;
        chem.potencyMgPerMl  = newMg;
        // Added 2026-08-04, closing the gap that made the allocator
        // structurally unable to weigh pH against Alk/Ca/Mg for existing
        // declared chemicals too, not just new ones.
        if (!doc["potencyPhPerMl"].isNull())  chem.potencyPhPerMl  = doc["potencyPhPerMl"];
        if (!doc["phSensitive"].isNull())     chem.phSensitive     = doc["phSensitive"];
    }

    if (!doc["maxMlPerDay"].isNull()) {
        float newCap = doc["maxMlPerDay"] | chem.maxMlPerDay;
        if (!isfinite(newCap) || newCap < 0.0f) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid maxMlPerDay\"}");
            return;
        }
        chem.maxMlPerDay = newCap;
    }

    // Added 2026-08-04: same edit pattern as maxMlPerDay above, for the two
    // fields that used to be pump-slot-only. See DeclaredChemical::
    // bucketThresholdMl's comment for the full reasoning.
    if (!doc["bucketThresholdMl"].isNull()) {
        float newVal = doc["bucketThresholdMl"] | chem.bucketThresholdMl;
        if (!isfinite(newVal) || newVal <= 0.0f) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid bucketThresholdMl\"}");
            return;
        }
        chem.bucketThresholdMl = newVal;
    }
    if (!doc["maxSingleDoseMl"].isNull()) {
        float newVal = doc["maxSingleDoseMl"] | chem.maxSingleDoseMl;
        if (!isfinite(newVal) || newVal <= 0.0f) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid maxSingleDoseMl\"}");
            return;
        }
        chem.maxSingleDoseMl = newVal;
    }

    if (!doc["nightFraction"].isNull()) {
        float newNf = doc["nightFraction"] | chem.nightFraction;
        if (!isfinite(newNf) || newNf < 0.0f || newNf > 100.0f) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"nightFraction must be 0-100\"}");
            return;
        }
        chem.nightFraction = newNf;
    }

    if (!doc["daytimeSuppressPercent"].isNull()) {
        float newDsp = doc["daytimeSuppressPercent"] | chem.daytimeSuppressPercent;
        if (!isfinite(newDsp) || newDsp < 0.0f || newDsp > 100.0f) {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"daytimeSuppressPercent must be 0-100\"}");
            return;
        }
        chem.daytimeSuppressPercent = newDsp;
    }

    if (!doc["active"].isNull()) chem.active = doc["active"] | chem.active;

    // Fixed 2026-08-02: same silent-failure gap as handlePostAddChemical --
    // confirmed root cause of tonight's reefDoser12 incident specifically
    // (an edited maxMlPerDay that the dashboard showed as saved, and that
    // WAS genuinely correct in memory at the time, silently reverted to
    // its old on-disk value after a later reboot). The in-memory edit
    // above and rebuildAiChemicalDeclarations() still take effect for the
    // rest of this boot regardless -- this only fixes the client-visible
    // success signal, so a real failure is no longer reported as success.
    bool saved = saveDeclaredChemicals();
    rebuildAiChemicalDeclarations();

    if (!saved) {
        server.send(500, "application/json",
            "{\"ok\":false,\"error\":\"Change applied but could not be saved to storage -- it will be LOST on next reboot. Try again.\"}");
        return;
    }
    logger.printf("CHEMICAL UPDATED: %s (id=%s)\n", chem.name.c_str(), id.c_str());

    calculateAiFromBestChemistry("ChemicalUpdate");
    publishAiPlanIfNeeded("ChemicalUpdate", true);

    server.send(200, "application/json", "{\"ok\":true}");
}

void handlePostRemoveChemical() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    String id = doc["id"] | "";
    int idx = findDeclaredChemicalIndexById(id);
    if (idx < 0) {
        server.send(404, "application/json", "{\"ok\":false,\"error\":\"Chemical id not found\"}");
        return;
    }

    // Sync ai.chemicals[] to declaredChemicals[] first so index idx refers
    // to the same chemical on both sides (see rebuildAiChemicalDeclarations()'s
    // comment on index correspondence).
    rebuildAiChemicalDeclarations();

    // §5.2 hard block (owner decision, 2026-07-24): reuses
    // AIEngineV2::removeChemical()'s own trial-removal + sufficiency check
    // rather than duplicating that logic here. Returns false and leaves
    // ai.chemicals[] untouched if removal would leave insufficient
    // capacity for current targets -- declaredChemicals[] (the persisted
    // list) is also left untouched in that case.
    if (!ai.removeChemical(idx)) {
        server.send(409, "application/json",
            "{\"ok\":false,\"error\":\"Removing this chemical would leave insufficient capacity for your current targets. Adjust targets or add another chemical first.\"}");
        return;
    }

    String removedName = declaredChemicals[idx].name;
    for (int i = idx; i < declaredChemicalCount - 1; i++) {
        declaredChemicals[i] = declaredChemicals[i + 1];
    }
    declaredChemicalCount--;
    // Fixed 2026-08-02: same silent-failure gap as the add/update handlers.
    bool saved = saveDeclaredChemicals();
    rebuildAiChemicalDeclarations(); // resync ai.chemicals[] to the now-shorter list

    if (!saved) {
        server.send(500, "application/json",
            "{\"ok\":false,\"error\":\"Removal applied but could not be saved to storage -- it will be UNDONE on next reboot. Try again.\"}");
        return;
    }

    Serial.printf("CHEMICAL REMOVED: %s (id=%s)\n", removedName.c_str(), id.c_str());
    logger.printf("CHEMICAL REMOVED: %s (id=%s)\n", removedName.c_str(), id.c_str());

    calculateAiFromBestChemistry("ChemicalRemove");
    publishAiPlanIfNeeded("ChemicalRemove", true);

    server.send(200, "application/json", "{\"ok\":true}");
}

// §8/§8.5 one-time setup wizard -- marks it finished so it doesn't show
// again. Does not itself validate that setup is actually complete (the
// engine's filters[p].initialized gate is the real safety backstop
// regardless of what this flag says); the wizard UI only calls this once
// its own required steps are satisfied.
void handlePostSetupWizardComplete() {
    saveSetupWizardCompleted(true);
    server.send(200, "application/json", "{\"ok\":true}");
}

// Lets a customer (or a test session) manually re-trigger the wizard later
// -- matches the "you can re-run this wizard anytime... if you overhaul
// your tank's setup" copy already shown on the wizard's own final step.
// Does NOT touch declaredChemicals[] -- shouldShowSetupWizard() only
// forces the wizard open if there's also no completed baseline test, so
// resetting this alone on an already-tested tank correctly does nothing
// until chemicals are also cleared (e.g. via the wizard's own "Clear All &
// Start Fresh").
void handlePostSetupWizardReset() {
    saveSetupWizardCompleted(false);
    server.send(200, "application/json", "{\"ok\":true}");
}

// §8.5 chemistry targets. Previously hardcoded compile-time constants with
// literally no customer-facing way to change them -- fixed 2026-07-25.
void handlePostChemistryTargets() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    float alk = doc["alk"] | NAN;
    float ca  = doc["ca"]  | NAN;
    float mg  = doc["mg"]  | NAN;

    // Same sanity bounds as handlePostManualTest()'s validation, matching
    // the range a real target should plausibly live in.
    if (!isfinite(alk) || alk < 4.0f || alk > 15.0f ||
        !isfinite(ca)  || ca  < 250.0f || ca > 700.0f ||
        !isfinite(mg)  || mg  < 800.0f || mg > 1800.0f) {
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"Target value outside a plausible reef-chemistry range\"}");
        return;
    }

    saveChemistryTargets(alk, ca, mg);
    server.send(200, "application/json", "{\"ok\":true}");
}

// Added 2026-08-05: companion to handlePostChemistryTargets() above, for
// the pH target range that was completely missing until now -- see
// main.cpp's targetPhLow/targetPhHigh declaration comment for why this
// mattered beyond just "no UI for it" (pH dosing correction silently
// never worked at all without a real target to compare against). Kept as
// its own endpoint/handler rather than folding into
// handlePostChemistryTargets() so that existing endpoint's request shape
// doesn't change for any client already calling it.
void handlePostPhTargetRange() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing JSON body\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    float lo = doc["phLow"]  | NAN;
    float hi = doc["phHigh"] | NAN;

    // Plausible reef-tank pH range, with a sane minimum band width so a
    // fat-fingered near-equal lo/hi doesn't produce a target midpoint that
    // is technically valid but useless (e.g. lo=8.20, hi=8.21).
    if (!isfinite(lo) || !isfinite(hi) ||
        lo < 7.0f || lo > 9.0f ||
        hi < 7.0f || hi > 9.0f ||
        hi <= lo || (hi - lo) < 0.05f) {
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"pH target range must be a plausible, well-separated low/high pair between 7.0 and 9.0\"}");
        return;
    }

    saveChemistryTargetPhRange(lo, hi);
    server.send(200, "application/json", "{\"ok\":true}");
}

void registerWebRoutes() {
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

        ai.tank.tankVolumeLiters = TANK_VOLUME_L; // v1 -> v2: direct field assignment, no gallons setter in v2
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

    // §5 free chemical declaration -- see DASHBOARD_MIGRATION_PLAN.md.
    // /api/dosing-mode above stays alive as a compatibility shim during
    // the Phase 1-3 transition; these are the real forward-looking API.
    server.on("/api/chemicals", HTTP_GET, handleGetChemicals);
    server.on("/api/chemicals", HTTP_POST, handlePostAddChemical);
    server.on("/api/chemicals/update", HTTP_POST, handlePostUpdateChemical);
    server.on("/api/chemicals/remove", HTTP_POST, handlePostRemoveChemical);
    server.on("/api/chemical-presets", HTTP_GET, handleGetChemicalPresets);
    server.on("/api/setup-wizard/complete", HTTP_POST, handlePostSetupWizardComplete);
    server.on("/api/setup-wizard/reset", HTTP_POST, handlePostSetupWizardReset);
    server.on("/api/config/chemistry-targets", HTTP_POST, handlePostChemistryTargets);
    server.on("/api/config/ph-target-range", HTTP_POST, handlePostPhTargetRange);

    server.on("/api/calibration", HTTP_POST, handlePostCalibration);
    server.on("/api/calibration-run", HTTP_POST, handlePostCalibrationRun);
    server.on("/api/live-dose", HTTP_POST, handlePostLiveDose);
    server.on("/api/chemical-levels", HTTP_GET, handleGetChemicalLevels);
    server.on("/api/chemical-levels", HTTP_POST, handlePostChemicalLevels);
    server.on("/api/emergency-stop", HTTP_POST, handlePostEmergencyStop);
    server.on("/api/reset-wifi", HTTP_POST, handlePostResetWifi);
    // Notification level endpoints.
    // /api/config/notifications is used by the updated dashboard.
    // /api/notifications is kept as a backward-compatible alias.
    server.on("/api/config/notifications", HTTP_GET, handleGetNotificationSettings);
    server.on("/api/config/notifications", HTTP_POST, handlePostNotificationSettings);
    server.on("/api/notifications", HTTP_GET, handleGetNotificationSettings);
    server.on("/api/notifications", HTTP_POST, handlePostNotificationSettings);

    server.on("/api/logger/force-upload", HTTP_POST, []() {
        logger.println("Manual logger upload requested from local API.");
        logGoogleDriveDiagnostics("force-upload-before");
        logger.forceUpload();
        logGoogleDriveDiagnostics("force-upload-after");
        publishLoggerHealthToFirebase("force-upload", true);
        server.send(200, "application/json", "{\"ok\":true,\"message\":\"Logger upload attempted; check WebSerial/serial and Firebase /loggerHealth\"}");
    });

    server.on("/api/logger/diagnostics", HTTP_GET, []() {
        GoogleDriveLogQueueStats q = collectGoogleDriveLogQueueStats();
        JsonDocument doc;
        doc["ok"] = true;
        doc["source"] = "google-drive-logger";
        doc["uptimeSec"] = (uint32_t)(millis() / 1000UL);
        doc["wifiStatus"] = (int)WiFi.status();
        doc["rssi"] = WiFi.RSSI();
        doc["ip"] = WiFi.localIP().toString();
        doc["gateway"] = WiFi.gatewayIP().toString();
        doc["dns"] = WiFi.dnsIP().toString();
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["minFreeHeap"] = ESP.getMinFreeHeap();
        doc["littleFsUsed"] = LittleFS.usedBytes();
        doc["littleFsTotal"] = LittleFS.totalBytes();
        doc["queuedFiles"] = q.fileCount;
        doc["queuedBytes"] = (uint32_t)q.totalBytes;
        doc["oldestFile"] = q.oldestPath;
        doc["oldestBytes"] = (uint32_t)q.oldestSize;
        doc["newestFile"] = q.newestPath;
        doc["newestBytes"] = (uint32_t)q.newestSize;

        String output;
        serializeJson(doc, output);
        server.send(200, "application/json", output);
    });
    server.on("/api/config/safeties", HTTP_POST, handlePostDosingSafeties);
    server.on("/api/config/ai-baseline", HTTP_POST, handlePostAiBaseline);
    // Removed 2026-08-04: routes for the 5 deleted handlers above.
    server.on("/api/config/lights", HTTP_POST, handlePostLightConfig);
    server.on("/api/config/mode7-split", HTTP_POST, handlePostMode7DayNightSplit);
    server.on("/api/config/ai-chemistry-safeties", HTTP_POST, handlePostAiChemistrySafeties);

    server.on("/api/history", HTTP_GET, []() {
        JsonDocument doc;

        JsonArray labels = doc.createNestedArray("labels");
        labels.add("Today");

        JsonObject params = doc.createNestedObject("params");
        JsonObject today = params.createNestedObject("Today");

        float count = (dailyStats.count > 0) ? (float)dailyStats.count : 1.0f;
        float totalDose = dailyDoseTotals[0] + dailyDoseTotals[1] + dailyDoseTotals[2] + dailyDoseTotals[3];

        // The dashboard water-parameter cards must show the newest accepted
        // chemistry, not today's running average. Daily averages remain stored
        // for the midnight history report, but they are not the live display.
        today["alk"] = currentAlk;
        today["ph"] = currentPh;
        today["temp"] = currentTempF;
        today["tempF"] = currentTempF;
        today["ca"] = currentCa;
        today["mg"] = currentMg;
        today["ppt"] = currentPpt;
        today["sal"] = currentPpt;
        today["sg"] = currentSg;
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
        planToday["kalk"] = currentPlan.kalk;
        planToday["afr"] = currentPlan.afr;
        planToday["alk"] = currentPlan.alk;
        planToday["cacl2"] = currentPlan.cacl2;
        planToday["naoh"] = currentPlan.naoh;
        planToday["mg"] = currentPlan.mg;

        doc["sampleCount"] = dailyStats.count;

        String output;
        serializeJson(doc, output);
        server.send(200, "application/json", output);
    });

}
