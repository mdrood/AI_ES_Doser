#include <FS.h>
#include <LittleFS.h>
#include "DemandLearning.h"
#include "WebRoutesShared.h"

// Functions below are moved verbatim from src/main.cpp.
// Behavior is unchanged - only the file they live in changed.

void loadAlkDemandLearningSetting() {
    prefs.begin("doser-settings", true);
    automaticDemandLearningEnabled = prefs.getBool("alk7_auto", false);
    prefs.end();

    Serial.printf("ALK 7-DAY AUTO LEARNING: %s\n",
                  automaticDemandLearningEnabled ? "ENABLED" : "DISABLED");
    logger.printf("ALK 7-DAY AUTO LEARNING: %s\n",
                  automaticDemandLearningEnabled ? "ENABLED" : "DISABLED");
}

void saveAlkDemandLearningSetting() {
    prefs.begin("doser-settings", false);
    prefs.putBool("alk7_auto", automaticDemandLearningEnabled);
    prefs.end();
}

void loadCalciumDemandLearningSetting() {
    prefs.begin("doser-settings", true);
    automaticCalciumLearningEnabled = prefs.getBool("ca7_auto", false);
    prefs.end();

    Serial.printf("CALCIUM 7-DAY AUTO LEARNING: %s\n",
                  automaticCalciumLearningEnabled ? "ENABLED" : "DISABLED");
    logger.printf("CALCIUM 7-DAY AUTO LEARNING: %s\n",
                  automaticCalciumLearningEnabled ? "ENABLED" : "DISABLED");
}

void saveCalciumDemandLearningSetting() {
    prefs.begin("doser-settings", false);
    prefs.putBool("ca7_auto", automaticCalciumLearningEnabled);
    prefs.end();
}


bool saveAlkDemandHistory() {
    File f = LittleFS.open(ALK_DEMAND_HISTORY_FILE, "w");
    if (!f) {
        Serial.println("ALK 7-DAY: failed to open LittleFS history for write.");
        logger.println("ALK 7-DAY: failed to open LittleFS history for write.");
        return false;
    }
    size_t wrote = f.write(reinterpret_cast<const uint8_t*>(&alkDemandStore), sizeof(alkDemandStore));
    f.close();
    return wrote == sizeof(alkDemandStore);
}

void loadAlkDemandHistory() {
    memset(&alkDemandStore, 0, sizeof(alkDemandStore));
    alkDemandStore.magic = ALK_DEMAND_MAGIC;
    alkDemandStore.version = ALK_DEMAND_VERSION;

    const bool fileExists = LittleFS.exists(ALK_DEMAND_HISTORY_FILE);
    File f = LittleFS.open(ALK_DEMAND_HISTORY_FILE, "r");
    bool valid = false;
    if (f && f.size() == sizeof(alkDemandStore)) {
        size_t got = f.read(reinterpret_cast<uint8_t*>(&alkDemandStore), sizeof(alkDemandStore));
        valid = got == sizeof(alkDemandStore) &&
                alkDemandStore.magic == ALK_DEMAND_MAGIC &&
                alkDemandStore.version == ALK_DEMAND_VERSION &&
                alkDemandStore.count <= ALK_DEMAND_MAX_DAYS;
    }
    if (f) f.close();

    if (!valid) {
        memset(&alkDemandStore, 0, sizeof(alkDemandStore));
        alkDemandStore.magic = ALK_DEMAND_MAGIC;
        alkDemandStore.version = ALK_DEMAND_VERSION;
        saveAlkDemandHistory();
    }

    Serial.printf("ALK 7-DAY INIT: file=%s valid=%s records=%u mode=%d device=%s\n",
                  fileExists ? "found" : "created",
                  valid ? "yes" : "new",
                  alkDemandStore.count,
                  dosingMode,
                  deviceID.c_str());
    logger.printf("ALK 7-DAY INIT: file=%s valid=%s records=%u mode=%d device=%s\n",
                  fileExists ? "found" : "created",
                  valid ? "yes" : "new",
                  alkDemandStore.count,
                  dosingMode,
                  deviceID.c_str());

    // Seed only reefDoser2 with the manually reviewed preceding week. This is
    // recommendation-only: it never changes a baseline or starts a pump.
}

void printAlkDemandRecommendation(const char* source) {
    float demand = alkDemandStore.recommendedDailyDemandDkh;

    Serial.println("========== ALK 7-DAY DEMAND ==========");
    Serial.printf("Source: %s\n", source ? source : "status");
    Serial.printf("History file: %s\n", LittleFS.exists(ALK_DEMAND_HISTORY_FILE) ? "FOUND" : "MISSING");
    Serial.printf("Completed daily records: %u / 7 required\n", alkDemandStore.count);

    if (demand <= 0.0f || !isfinite(demand)) {
        Serial.println("Recommendation: waiting for seven completed daily records.");
        Serial.printf("Automatic baseline changes: %s\n", automaticDemandLearningEnabled ? "ENABLED" : "DISABLED");
        Serial.println("======================================");
        return;
    }

    const float kalkStrength = kalkStrengthDkhPerMl;
    const float naohStrength = naohStrengthDkhPerMl;
    const float alkStrength = alkStrengthDkhPerMl;
    float fixedDkh = baselineKalkMlDay * kalkStrength + baselineNaohMlDay * naohStrength;
    float recommendedP4 = 0.0f;
    if (alkStrength > 0.0f) {
        recommendedP4 = (demand - fixedDkh) / alkStrength;
    }
    if (!isfinite(recommendedP4) || recommendedP4 < 0.0f) recommendedP4 = 0.0f;
    if (recommendedP4 > aiMaxAlkDayMl) recommendedP4 = aiMaxAlkDayMl;
    alkDemandStore.lastRecommendedP4MlDay = recommendedP4;
    saveAlkDemandHistory();

    Serial.printf("Recommended total Alk demand: %.3f dKH/day\n", demand);
    Serial.printf("Existing Kalk + NaOH baseline: %.3f dKH/day\n", fixedDkh);
    Serial.printf("Current P4 Alk baseline: %.2f ml/day\n", baselineMgMlDay);
    Serial.printf("Recommended P4 Alk baseline: %.2f ml/day\n", recommendedP4);
    Serial.printf("Learning mode: %s\n",
                  automaticDemandLearningEnabled ? "ENABLED - valid rolling calculations may adjust P4 baseline" :
                                                   "RECOMMENDATION ONLY - no baseline change");
    Serial.printf("Automatic baseline changes: %s\n", automaticDemandLearningEnabled ? "ENABLED" : "DISABLED");
    Serial.println("======================================");

    logger.printf("ALK 7-DAY RECOMMEND [%s]: demand=%.3f fixed=%.3f currentP4=%.2f recommendedP4=%.2f NOT_APPLIED\n",
                  source ? source : "status", demand, fixedDkh,
                  baselineMgMlDay, recommendedP4);
}

bool publishAlkDemandStatusToFirebase(const char* source, bool force) {
    if (WiFi.status() != WL_CONNECTED || !firebaseStarted || !Firebase.ready()) {
        return false;
    }

    static unsigned long lastPublishMs = 0;
    const unsigned long nowMs = millis();
    if (!force && lastPublishMs != 0 && (nowMs - lastPublishMs) < 300000UL) {
        return false;
    }

    float demand = alkDemandStore.recommendedDailyDemandDkh;
    float fixedDkh = baselineKalkMlDay * kalkStrengthDkhPerMl +
                     baselineNaohMlDay * naohStrengthDkhPerMl;
    float recommendedP4 = alkDemandStore.lastRecommendedP4MlDay;
    if ((!isfinite(recommendedP4) || recommendedP4 < 0.0f) && alkStrengthDkhPerMl > 0.0f && demand > 0.0f) {
        recommendedP4 = (demand - fixedDkh) / alkStrengthDkhPerMl;
    }
    if (!isfinite(recommendedP4) || recommendedP4 < 0.0f) recommendedP4 = 0.0f;
    if (recommendedP4 > aiMaxAlkDayMl) recommendedP4 = aiMaxAlkDayMl;

    float measuredDemand = 0.0f;
    float startAlk = 0.0f;
    float endAlk = 0.0f;
    uint32_t windowStartDay = 0;
    uint32_t windowEndDay = 0;

    if (alkDemandStore.count >= 7) {
        const uint8_t firstIndex = alkDemandStore.count - 7;
        const AlkDemandDay& first = alkDemandStore.days[firstIndex];
        const AlkDemandDay& last = alkDemandStore.days[alkDemandStore.count - 1];
        float addedDkh = 0.0f;
        for (uint8_t i = firstIndex; i < alkDemandStore.count; ++i) {
            addedDkh += alkDemandStore.days[i].kalkMl * kalkStrengthDkhPerMl;
            addedDkh += alkDemandStore.days[i].afrMl * afrStrengthDkhPerMl;
            addedDkh += alkDemandStore.days[i].naohMl * naohStrengthDkhPerMl;
            addedDkh += alkDemandStore.days[i].alkMl * alkStrengthDkhPerMl;
        }
        startAlk = first.avgAlk;
        endAlk = last.avgAlk;
        windowStartDay = first.dayKey;
        windowEndDay = last.dayKey;
        measuredDemand = (addedDkh - (endAlk - startAlk)) / 7.0f;
        if (!isfinite(measuredDemand) || measuredDemand < 0.0f) measuredDemand = 0.0f;
    }

    FirebaseJson json;
    json.set("source", source ? source : "alkDemand");
    json.set("daysCollected", (int)min((uint8_t)7, alkDemandStore.count));
    json.set("storedRecords", (int)alkDemandStore.count);
    json.set("ready", alkDemandStore.count >= 7);
    json.set("automaticChanges", automaticDemandLearningEnabled);
    json.set("recommendedDemandDkhDay", demand);
    json.set("measuredDemandDkhDay", measuredDemand);
    json.set("currentKalkBaselineMlDay", baselineKalkMlDay);
    json.set("currentNaohBaselineMlDay", baselineNaohMlDay);
    json.set("currentP4BaselineMlDay", baselineMgMlDay);
    json.set("fixedKalkNaohDkhDay", fixedDkh);
    json.set("recommendedP4MlDay", recommendedP4);
    json.set("startAlk", startAlk);
    json.set("endAlk", endAlk);
    json.set("windowStartDay", (uint32_t)windowStartDay);
    json.set("windowEndDay", (uint32_t)windowEndDay);
    json.set("updatedAtEpoch", (uint32_t)time(nullptr));

    String path = "/devices/" + deviceID + "/alkDemand";
    if (Firebase.updateNode(writeFbdo, path.c_str(), json)) {
        lastPublishMs = nowMs;
        logger.printf("ALK 7-DAY FIREBASE OK [%s]: days=%u demand=%.3f measured=%.3f recommendedP4=%.2f\n",
                      source ? source : "alkDemand",
                      min((uint8_t)7, alkDemandStore.count),
                      demand, measuredDemand, recommendedP4);
        return true;
    }

    logger.printf("ALK 7-DAY FIREBASE FAILED [%s]: %s\n",
                  source ? source : "alkDemand", writeFbdo.errorReason().c_str());
    return false;
}

void recordCompletedDayAndLearn(float avgAlk) {
    if (dosingMode < 1 || dosingMode > 8 || !isfinite(avgAlk) || avgAlk <= 0.0f) return;

    AlkDemandDay day;
    day.dayKey = localDayKeyWithOffsetDays(-1);
    day.avgAlk = avgAlk;
    day.mode = (uint8_t)dosingMode;
    // Convert physical pump totals into alkalinity-source totals for every mode.
    switch (dosingMode) {
        case 1: day.kalkMl = dailyDoseTotals[0]; break;
        case 2: day.afrMl = dailyDoseTotals[0]; break;
        case 3: day.kalkMl = dailyDoseTotals[0]; day.afrMl = dailyDoseTotals[1]; break;
        case 4: day.alkMl = dailyDoseTotals[0]; break;
        case 5: day.kalkMl = dailyDoseTotals[0]; day.alkMl = dailyDoseTotals[1]; break;
        case 6: day.kalkMl = dailyDoseTotals[0]; day.naohMl = dailyDoseTotals[2]; break;
        case 7: day.kalkMl = dailyDoseTotals[0]; day.naohMl = dailyDoseTotals[2]; day.alkMl = dailyDoseTotals[3]; break;
        case 8: day.kalkMl = dailyDoseTotals[0]; day.naohMl = dailyDoseTotals[2]; break;
    }

    if (alkDemandStore.count > 0 && alkDemandStore.days[alkDemandStore.count - 1].dayKey == day.dayKey) {
        alkDemandStore.days[alkDemandStore.count - 1] = day;
    } else {
        if (alkDemandStore.count >= ALK_DEMAND_MAX_DAYS) {
            memmove(&alkDemandStore.days[0], &alkDemandStore.days[1],
                    sizeof(AlkDemandDay) * (ALK_DEMAND_MAX_DAYS - 1));
            alkDemandStore.count = ALK_DEMAND_MAX_DAYS - 1;
        }
        alkDemandStore.days[alkDemandStore.count++] = day;
    }

    Serial.printf("ALK 7-DAY DAILY RECORD: day=%lu avgAlk=%.3f kalk=%.2f naoh=%.2f alk=%.2f records=%u\n",
                  (unsigned long)day.dayKey, day.avgAlk, day.kalkMl,
                  day.naohMl, day.alkMl, alkDemandStore.count);
    logger.println("========== ALK 7-DAY DAILY SUMMARY ==========");
    logger.printf("Day: %lu\n", (unsigned long)day.dayKey);
    logger.printf("Average Alk: %.3f dKH\n", day.avgAlk);
    logger.printf("Mode: %u\n", day.mode);
    logger.printf("Actual Kalk delivered: %.2f ml\n", day.kalkMl);
    logger.printf("Actual AFR delivered: %.2f ml\n", day.afrMl);
    logger.printf("Actual NaOH delivered: %.2f ml\n", day.naohMl);
    logger.printf("Actual P4 Alk delivered: %.2f ml\n", day.alkMl);
    logger.printf("Equivalent Alk added: Kalk=%.3f NaOH=%.3f P4=%.3f total=%.3f dKH\n",
                  day.kalkMl * kalkStrengthDkhPerMl,
                  day.naohMl * naohStrengthDkhPerMl,
                  day.alkMl * alkStrengthDkhPerMl,
                  day.kalkMl * kalkStrengthDkhPerMl +
                  day.afrMl * afrStrengthDkhPerMl +
                  day.naohMl * naohStrengthDkhPerMl +
                  day.alkMl * alkStrengthDkhPerMl);
    logger.printf("LittleFS record count: %u / 7 required\n", alkDemandStore.count);
    logger.printf("Automatic baseline changes: %s\n", automaticDemandLearningEnabled ? "ENABLED" : "DISABLED");
    logger.println("==============================================");

    if (alkDemandStore.count >= 7) {
        const uint8_t firstIndex = alkDemandStore.count - 7;
        const AlkDemandDay& first = alkDemandStore.days[firstIndex];
        const AlkDemandDay& last = alkDemandStore.days[alkDemandStore.count - 1];

        float addedDkh = 0.0f;
        for (uint8_t i = firstIndex; i < alkDemandStore.count; ++i) {
            addedDkh += alkDemandStore.days[i].kalkMl * kalkStrengthDkhPerMl;
            addedDkh += alkDemandStore.days[i].afrMl * afrStrengthDkhPerMl;
            addedDkh += alkDemandStore.days[i].naohMl * naohStrengthDkhPerMl;
            addedDkh += alkDemandStore.days[i].alkMl * alkStrengthDkhPerMl;
        }

        float alkChange = last.avgAlk - first.avgAlk;
        float measuredDemand = (addedDkh - alkChange) / 7.0f;
        if (isfinite(measuredDemand) && measuredDemand >= 0.05f && measuredDemand <= aiMaxAlkRiseDkhDay) {
            float oldRecommendation = alkDemandStore.recommendedDailyDemandDkh > 0.0f
                                          ? alkDemandStore.recommendedDailyDemandDkh
                                          : measuredDemand;
            float proposed = oldRecommendation + (measuredDemand - oldRecommendation) * 0.25f;
            float low = oldRecommendation * 0.90f;
            float high = oldRecommendation * 1.10f;
            proposed = constrain(proposed, low, high);
            proposed = constrain(proposed, 0.05f, aiMaxAlkRiseDkhDay);
            alkDemandStore.recommendedDailyDemandDkh = proposed;

            bool applied = false;
            float appliedP4Baseline = baselineMgMlDay;
            float targetP4Baseline = 0.0f;
            const float alkStrength = alkStrengthDkhPerMl;
            const float fixedDkh = baselineKalkMlDay * kalkStrengthDkhPerMl +
                                   baselineNaohMlDay * naohStrengthDkhPerMl;

            if (alkStrength > 0.0f) {
                targetP4Baseline = (proposed - fixedDkh) / alkStrength;
            }
            if (!isfinite(targetP4Baseline) || targetP4Baseline < 0.0f) targetP4Baseline = 0.0f;
            if (targetP4Baseline > aiMaxAlkDayMl) targetP4Baseline = aiMaxAlkDayMl;
            alkDemandStore.lastRecommendedP4MlDay = targetP4Baseline;

            if (automaticDemandLearningEnabled && alkStrength > 0.0f) {
                // Apply only a bounded step. Existing nonzero P4 baseline may move at most 10%.
                // A zero baseline begins at only 10% of the calculated target, preventing a sudden start.
                float lowP4 = baselineMgMlDay > 0.0f ? baselineMgMlDay * 0.90f : 0.0f;
                float highP4 = baselineMgMlDay > 0.0f ? baselineMgMlDay * 1.10f : targetP4Baseline * 0.10f;
                if (highP4 < 0.0f) highP4 = 0.0f;
                appliedP4Baseline = constrain(targetP4Baseline, lowP4, highP4);
                appliedP4Baseline = constrain(appliedP4Baseline, 0.0f, aiMaxAlkDayMl);

                if (fabsf(appliedP4Baseline - baselineMgMlDay) >= 0.01f) {
                    float oldP4Baseline = baselineMgMlDay;
                    baselineMgMlDay = appliedP4Baseline;

                    prefs.begin("doser-settings", false);
                    prefs.putFloat("base_mg", baselineMgMlDay);
                    prefs.end();

                    applyAiBaselineToEngine();
                    applied = true;

                    Serial.printf("ALK 7-DAY APPLIED: P4 baseline %.2f -> %.2f ml/day target=%.2f ml/day\n",
                                  oldP4Baseline, baselineMgMlDay, targetP4Baseline);
                    logger.printf("ALK 7-DAY APPLIED: P4 baseline %.2f -> %.2f ml/day target=%.2f ml/day\n",
                                  oldP4Baseline, baselineMgMlDay, targetP4Baseline);
                } else {
                    logger.printf("ALK 7-DAY ENABLED: recommendation required no P4 baseline change. current=%.2f target=%.2f\n",
                                  baselineMgMlDay, targetP4Baseline);
                }
            }

            Serial.printf("ALK 7-DAY CALC: measured=%.3f previousRecommendation=%.3f newRecommendation=%.3f added=%.3f alkChange=%+.3f applied=%s\n",
                          measuredDemand, oldRecommendation, proposed, addedDkh, alkChange,
                          applied ? "yes" : "no");
            logger.println("========== ALK 7-DAY DEMAND ANALYSIS ==========");
            logger.printf("Window: %lu through %lu\n",
                          (unsigned long)first.dayKey, (unsigned long)last.dayKey);
            logger.printf("Starting Alk: %.3f dKH\n", first.avgAlk);
            logger.printf("Ending Alk: %.3f dKH\n", last.avgAlk);
            logger.printf("Seven-day Alk change: %+.3f dKH\n", alkChange);
            logger.printf("Total Alk added: %.3f dKH\n", addedDkh);
            logger.printf("Measured tank demand: %.3f dKH/day\n", measuredDemand);
            logger.printf("Previous recommendation: %.3f dKH/day\n", oldRecommendation);
            logger.printf("New recommendation: %.3f dKH/day\n", proposed);
            logger.printf("Calculated P4 target: %.2f ml/day\n", targetP4Baseline);
            logger.printf("Current P4 baseline after calculation: %.2f ml/day\n", baselineMgMlDay);
            logger.printf("Applied this calculation: %s\n", applied ? "YES" : "NO");
            logger.printf("Automatic baseline changes: %s\n", automaticDemandLearningEnabled ? "ENABLED" : "DISABLED");
            logger.println("===============================================");
        } else {
            Serial.printf("ALK 7-DAY REJECTED: calculated demand %.3f dKH/day outside safety range.\n",
                          measuredDemand);
            logger.printf("ALK 7-DAY REJECTED: calculated demand %.3f dKH/day outside safety range.\n",
                          measuredDemand);
        }
    } else {
        Serial.printf("ALK 7-DAY WAITING: %u more completed day(s) needed.\n",
                      7U - alkDemandStore.count);
        logger.printf("ALK 7-DAY WAITING: %u more completed day(s) needed.\n",
                      7U - alkDemandStore.count);
    }

    saveAlkDemandHistory();
    printAlkDemandRecommendation("midnight");
    publishAlkDemandStatusToFirebase("midnight", true);
}

bool saveCalciumDemandHistory() {
    File f = LittleFS.open(CA_DEMAND_HISTORY_FILE, "w");
    if (!f) return false;
    size_t wrote = f.write(reinterpret_cast<const uint8_t*>(&calciumDemandStore),
                           sizeof(calciumDemandStore));
    f.close();
    return wrote == sizeof(calciumDemandStore);
}

void loadCalciumDemandHistory() {
    memset(&calciumDemandStore, 0, sizeof(calciumDemandStore));
    calciumDemandStore.magic = CA_DEMAND_MAGIC;
    calciumDemandStore.version = CA_DEMAND_VERSION;

    bool valid = false;
    File f = LittleFS.open(CA_DEMAND_HISTORY_FILE, "r");
    if (f && f.size() == sizeof(calciumDemandStore)) {
        size_t got = f.read(reinterpret_cast<uint8_t*>(&calciumDemandStore),
                            sizeof(calciumDemandStore));
        valid = got == sizeof(calciumDemandStore) &&
                calciumDemandStore.magic == CA_DEMAND_MAGIC &&
                calciumDemandStore.version == CA_DEMAND_VERSION &&
                calciumDemandStore.count <= CA_DEMAND_MAX_DAYS;
    }
    if (f) f.close();

    if (!valid) {
        memset(&calciumDemandStore, 0, sizeof(calciumDemandStore));
        calciumDemandStore.magic = CA_DEMAND_MAGIC;
        calciumDemandStore.version = CA_DEMAND_VERSION;
        saveCalciumDemandHistory();
    }

    Serial.printf("CALCIUM 7-DAY INIT: file=%s valid=%s records=%u mode=%d device=%s\n",
                  LittleFS.exists(CA_DEMAND_HISTORY_FILE) ? "found" : "missing",
                  valid ? "yes" : "new",
                  calciumDemandStore.count, dosingMode, deviceID.c_str());
    logger.printf("CALCIUM 7-DAY INIT: file=%s valid=%s records=%u mode=%d device=%s\n",
                  LittleFS.exists(CA_DEMAND_HISTORY_FILE) ? "found" : "missing",
                  valid ? "yes" : "new",
                  calciumDemandStore.count, dosingMode, deviceID.c_str());
}

void printCalciumDemandRecommendation(const char* source) {
    Serial.println("========== CALCIUM 7-DAY DEMAND ==========");
    Serial.printf("Source: %s\n", source ? source : "unknown");
    Serial.printf("Completed daily records: %u / 7 required\n", calciumDemandStore.count);
    if (calciumDemandStore.recommendedDailyDemandPpm > 0.0f) {
        Serial.printf("Recommended Calcium demand: %.3f ppm/day\n",
                      calciumDemandStore.recommendedDailyDemandPpm);
        Serial.printf("Current P2 CaCl2 baseline: %.2f ml/day\n", baselineCacl2MlDay);
        Serial.printf("Recommended P2 CaCl2 baseline: %.2f ml/day\n",
                      calciumDemandStore.lastRecommendedP2MlDay);
    } else {
        Serial.println("Recommendation: waiting for seven completed daily records.");
    }
    Serial.printf("Automatic baseline changes: %s\n",
                  automaticCalciumLearningEnabled ? "ENABLED" : "DISABLED");
    Serial.println("==========================================");
}

bool publishCalciumDemandStatusToFirebase(const char* source, bool force) {
    static uint32_t lastPublishMs = 0;
    const uint32_t nowMs = millis();
    if (!force && nowMs - lastPublishMs < 60000UL) return false;
    if (!Firebase.ready()) return false;

    float startCa = 0.0f;
    float endCa = 0.0f;
    float measuredDemand = 0.0f;
    if (calciumDemandStore.count >= 7) {
        const CalciumDemandDay& first = calciumDemandStore.days[0];
        const CalciumDemandDay& last = calciumDemandStore.days[calciumDemandStore.count - 1];
        startCa = first.avgCa;
        endCa = last.avgCa;

        float addedPpm = 0.0f;
        for (uint8_t i = 0; i < calciumDemandStore.count; ++i) {
            addedPpm += calciumDemandStore.days[i].cacl2Ml * cacl2StrengthPpmPerMl;
            addedPpm += calciumDemandStore.days[i].kalkMl *
                        kalkStrengthDkhPerMl * CA_PPM_PER_DKH_KALK;
        }
        measuredDemand = (addedPpm - (endCa - startCa)) / 7.0f;
    }

    FirebaseJson json;
    json.set("source", source ? source : "calciumDemand");
    json.set("daysCollected", (int)calciumDemandStore.count);
    json.set("ready", calciumDemandStore.count >= 7);
    json.set("automaticChanges", automaticCalciumLearningEnabled);
    json.set("recommendedDemandPpmDay", calciumDemandStore.recommendedDailyDemandPpm);
    json.set("measuredDemandPpmDay", measuredDemand);
    json.set("currentP2BaselineMlDay", baselineCacl2MlDay);
    json.set("recommendedP2MlDay", calciumDemandStore.lastRecommendedP2MlDay);
    json.set("startCa", startCa);
    json.set("endCa", endCa);
    json.set("updatedAtEpoch", (uint32_t)time(nullptr));

    String path = "/devices/" + deviceID + "/calciumDemand";
    if (Firebase.updateNode(writeFbdo, path.c_str(), json)) {
        lastPublishMs = nowMs;
        logger.printf("CALCIUM 7-DAY FIREBASE OK [%s]: days=%u demand=%.3f measured=%.3f recommendedP2=%.2f\n",
                      source ? source : "calciumDemand",
                      calciumDemandStore.count,
                      calciumDemandStore.recommendedDailyDemandPpm,
                      measuredDemand,
                      calciumDemandStore.lastRecommendedP2MlDay);
        return true;
    }

    logger.printf("CALCIUM 7-DAY FIREBASE FAILED [%s]: %s\n",
                  source ? source : "calciumDemand",
                  writeFbdo.errorReason().c_str());
    return false;
}

void recordCompletedCalciumDayAndLearn(float avgCa) {
    if (dosingMode < 1 || dosingMode > 8 ||
        !isfinite(avgCa) || avgCa < 250.0f || avgCa > 650.0f) return;

    CalciumDemandDay day;
    day.dayKey = localDayKeyWithOffsetDays(-1);
    day.avgCa = avgCa;
    day.mode = (uint8_t)dosingMode;

    // Record actual delivered chemistry using the physical mapping for each mode.
    switch (dosingMode) {
        case 1: day.kalkMl = dailyDoseTotals[0]; break;
        case 2: day.afrMl = dailyDoseTotals[0]; break;
        case 3:
            day.kalkMl = dailyDoseTotals[0];
            day.afrMl = dailyDoseTotals[1];
            break;
        case 4: day.cacl2Ml = dailyDoseTotals[1]; break;
        case 5:
            day.kalkMl = dailyDoseTotals[0];
            day.cacl2Ml = dailyDoseTotals[2];
            break;
        case 6:
        case 7:
        case 8:
            day.kalkMl = dailyDoseTotals[0];
            day.cacl2Ml = dailyDoseTotals[1];
            break;
    }

    if (calciumDemandStore.count > 0 &&
        calciumDemandStore.days[calciumDemandStore.count - 1].dayKey == day.dayKey) {
        calciumDemandStore.days[calciumDemandStore.count - 1] = day;
    } else {
        if (calciumDemandStore.count >= CA_DEMAND_MAX_DAYS) {
            memmove(&calciumDemandStore.days[0], &calciumDemandStore.days[1],
                    sizeof(CalciumDemandDay) * (CA_DEMAND_MAX_DAYS - 1));
            calciumDemandStore.count = CA_DEMAND_MAX_DAYS - 1;
        }
        calciumDemandStore.days[calciumDemandStore.count++] = day;
    }

    if (calciumDemandStore.count >= 7) {
        const CalciumDemandDay& first = calciumDemandStore.days[0];
        const CalciumDemandDay& last = calciumDemandStore.days[calciumDemandStore.count - 1];
        float addedPpm = 0.0f;
        for (uint8_t i = 0; i < calciumDemandStore.count; ++i) {
            const CalciumDemandDay& x = calciumDemandStore.days[i];
            addedPpm += x.cacl2Ml * cacl2StrengthPpmPerMl;
            addedPpm += (x.kalkMl * kalkStrengthDkhPerMl +
                         x.afrMl * afrStrengthDkhPerMl) * CA_PPM_PER_DKH_KALK;
        }
        const float measuredDemand = (addedPpm - (last.avgCa - first.avgCa)) / 7.0f;
        if (isfinite(measuredDemand) && measuredDemand >= 0.0f && measuredDemand <= 50.0f) {
            float oldRec = calciumDemandStore.recommendedDailyDemandPpm > 0.0f
                         ? calciumDemandStore.recommendedDailyDemandPpm : measuredDemand;
            float proposed = oldRec + (measuredDemand - oldRec) * 0.25f;
            proposed = constrain(proposed, oldRec * 0.90f, oldRec * 1.10f);
            calciumDemandStore.recommendedDailyDemandPpm = constrain(proposed, 0.0f, 50.0f);
        }
    }

    saveCalciumDemandHistory();
    printCalciumDemandRecommendation("midnight");
    publishCalciumDemandStatusToFirebase("midnight", true);
    logger.printf("CALCIUM LEARNER ALL-MODE: mode=%d avgCa=%.2f kalk=%.2f afr=%.2f cacl2=%.2f records=%u learned=%.3f ppm/day\n",
                  dosingMode, day.avgCa, day.kalkMl, day.afrMl, day.cacl2Ml,
                  calciumDemandStore.count, calciumDemandStore.recommendedDailyDemandPpm);
}

