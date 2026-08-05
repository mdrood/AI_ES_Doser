#ifndef AI_ENGINE_H
#define AI_ENGINE_H

#include <Arduino.h>

struct DosingPlan {
    float kalk; float afr; float alk; float cacl2; float naoh; float mg; // Changed from double to float to fix warnings
    bool active = false;
};

struct HistoryEntry {
    uint32_t timestamp;
    DosingPlan plan;
    int mode;
};

class AIEngine {
public:
    void calculateNextPlan(int mode, float consAlk, float consCa, float consMg, float currentPh, bool lightsActive);
    void update(); // Wrapper for main.cpp
    DosingPlan currentPlan;
    void setTankVolumeGallons(float gallons) { 
        _tankVolumeLiters = gallons * 3.78541f; 
    }
    void setBaselineDemand(float kalkMlDay, float cacl2MlDay, float naohMlDay, float mgMlDay) {
        baselineKalkMlDay = kalkMlDay;
        baselineCacl2MlDay = cacl2MlDay;
        baselineNaohMlDay = naohMlDay;
        baselineMgMlDay = mgMlDay;
    }

    void setChemicalStrengths(float kalkDkhPerMl, float afrDkhPerMl, float alkDkhPerMl,
                              float naohDkhPerMl, float mgPpmPerMl, float cacl2PpmPerMl) {
        if (isfinite(kalkDkhPerMl) && kalkDkhPerMl > 0.0f) chem.dkhPerMlKalk = kalkDkhPerMl;
        if (isfinite(afrDkhPerMl) && afrDkhPerMl > 0.0f) chem.dkhPerMlAfr = afrDkhPerMl;
        if (isfinite(alkDkhPerMl) && alkDkhPerMl > 0.0f) chem.dkhPerMlAlk = alkDkhPerMl;
        if (isfinite(naohDkhPerMl) && naohDkhPerMl > 0.0f) chem.dkhPerMlNaoh = naohDkhPerMl;
        if (isfinite(mgPpmPerMl) && mgPpmPerMl > 0.0f) chem.mgPerMlMg = mgPpmPerMl;
        if (isfinite(cacl2PpmPerMl) && cacl2PpmPerMl > 0.0f) chem.caPerMlCacl2 = cacl2PpmPerMl;
    }

    float getDkhPerMlKalk() const { return chem.dkhPerMlKalk; }
    float getDkhPerMlAfr() const { return chem.dkhPerMlAfr; }
    float getDkhPerMlAlk() const { return chem.dkhPerMlAlk; }
    float getDkhPerMlNaoh() const { return chem.dkhPerMlNaoh; }
    float getMgPerMlMg() const { return chem.mgPerMlMg; }
    float getCaPerMlCacl2() const { return chem.caPerMlCacl2; }


    void setMode7DayNightSplit(float dayNaohPct, float dayAlkPct,
                                float nightNaohPct, float nightAlkPct,
                                float naohMaxPh, bool enabled) {
        mode7Split.enabled = enabled;
        mode7Split.dayNaohPct = constrain(dayNaohPct, 0.0f, 100.0f);
        mode7Split.dayAlkPct = constrain(dayAlkPct, 0.0f, 100.0f);
        mode7Split.nightNaohPct = constrain(nightNaohPct, 0.0f, 100.0f);
        mode7Split.nightAlkPct = constrain(nightAlkPct, 0.0f, 100.0f);
        mode7Split.naohMaxPh = constrain(naohMaxPh, 7.80f, 8.80f);
    }

    bool getMode7SplitEnabled() const { return mode7Split.enabled; }
    float getMode7DayNaohPct() const { return mode7Split.dayNaohPct; }
    float getMode7DayAlkPct() const { return mode7Split.dayAlkPct; }
    float getMode7NightNaohPct() const { return mode7Split.nightNaohPct; }
    float getMode7NightAlkPct() const { return mode7Split.nightAlkPct; }
    float getMode7NaohMaxPh() const { return mode7Split.naohMaxPh; }

    void setChemistrySafetyLimits(float maxKalkDay, float maxNaohDay, float maxAlkDay,
                                  float maxAlkRisePerDay, float maxMgCorrectionDay,
                                  float maxMgDay, float mgDeadbandPpm) {
        if (isfinite(maxKalkDay) && maxKalkDay > 0.0f) limits.maxKalkDay = maxKalkDay;
        if (isfinite(maxNaohDay) && maxNaohDay > 0.0f) limits.maxNaohDay = maxNaohDay;
        if (isfinite(maxAlkDay) && maxAlkDay > 0.0f) limits.maxAlkDay = maxAlkDay;
        if (isfinite(maxAlkRisePerDay) && maxAlkRisePerDay > 0.0f) limits.maxAlkRisePerDay = maxAlkRisePerDay;
        if (isfinite(maxMgCorrectionDay) && maxMgCorrectionDay > 0.0f) limits.maxMgCorrectionDay = maxMgCorrectionDay;
        if (isfinite(maxMgDay) && maxMgDay > 0.0f) limits.maxMgDay = maxMgDay;
        if (isfinite(mgDeadbandPpm) && mgDeadbandPpm >= 0.0f) limits.mgDeadbandPpm = mgDeadbandPpm;
    }

    float getMaxKalkDay() const { return limits.maxKalkDay; }
    float getMaxNaohDay() const { return limits.maxNaohDay; }
    float getMaxAlkDay() const { return limits.maxAlkDay; }
    float getMaxAlkRisePerDay() const { return limits.maxAlkRisePerDay; }
    float getMaxMgCorrectionDay() const { return limits.maxMgCorrectionDay; }
    float getMaxMgDay() const { return limits.maxMgDay; }
    float getMgDeadbandPpm() const { return limits.mgDeadbandPpm; }

    // Per-pump accumulator dump thresholds in mL.
    // Eric Mode 7 physical pump map:
    //   P1 = Kalk, P2 = CaCl2, P3 = NaOH, P4 = Alk
    // Setting P3 to 100.0 means NaOH must accumulate to 100 mL before dosing.
    float getPumpDumpThresholdMl(uint8_t pump) const {
        switch (pump) {
            case 1: return dumpThresholds.p1DumpMl;
            case 2: return dumpThresholds.p2DumpMl;
            case 3: return dumpThresholds.p3DumpMl;
            case 4: return dumpThresholds.p4DumpMl;
            default: return 1.0f;
        }
    }

private:
    float _tankVolumeLiters = 1135.6f;

    // User-provided daily baseline demand. The AI adjusts up/down from these
    // known tank consumption values instead of guessing total demand from zero.
    float baselineKalkMlDay = 0.0f;
    float baselineCacl2MlDay = 0.0f;
    float baselineNaohMlDay = 0.0f;
    float baselineMgMlDay = 0.0f;

    // ====== RESTORED CHEMISTRY STRENGTH STAGE ======
    struct {
        // Mark (300g) Inactive
        //float dkhPerMlKalk = 0.0000085f, dkhPerMlAfr = 0.00015f, dkhPerMlAlk = 0.0050f, dkhPerMlNaoh = 0.00255f, mgPerMlMg = 0.0050f, caPerMlCacl2 = 0.42f;
        // Eric (1100g) Active
        float dkhPerMlKalk = 0.0000046f, dkhPerMlAfr = 0.000082f, dkhPerMlAlk = 0.00126f, dkhPerMlNaoh = 0.00139f, mgPerMlMg = 0.00273f, caPerMlCacl2 = 0.0545f;
    } chem;

    // ====== PER-PUMP ACCUMULATOR / DUMP THRESHOLDS ======
    struct {
        // Eric Mode 7 accumulator thresholds:
        //   P1 = Kalk, P2 = CaCl2, P3 = NaOH, P4 = Alk
        // Each pump must accumulate to this mL amount before it dumps.
        //mark's
/*        float p1DumpMl = 20.0f;
        float p2DumpMl = 20.0f;
        float p3DumpMl = 20.0f;
        float p4DumpMl = 20.0f;*/
        //eric's
        float p1DumpMl = 100.0f;
        float p2DumpMl = 10.0f;
        float p3DumpMl = 5.0f;//needs to be 10 with smaller pumps
        float p4DumpMl = 5.0f;//needs to be 25 with smaller pumps
    } dumpThresholds;

    // ====== SAFETY LIMITS ======
    struct {
        // Mark (300g) Inactive
        //float maxKalkDay = 2500.0f, maxNaohDay = 100.0f, maxAlkDay = 500.0f, maxAlkRisePerDay = 1.5f;
        //float maxMgCorrectionDay = 250.0f, maxMgDay = 250.0f, mgDeadbandPpm = 25.0f;

        // Eric large reef tuned example (Active):
        float maxKalkDay = 35000.0f, maxNaohDay = 1200.0f, maxAlkDay = 2500.0f, maxAlkRisePerDay = 2.0f;
        float maxMgCorrectionDay = 250.0f, maxMgDay = 250.0f, mgDeadbandPpm = 25.0f;
    } limits;


    // ====== MODE 7 DAY/NIGHT ALK SOURCE SPLIT ======
    struct {
        // Enabled by default for Eric test: day favors P4 Alk, night favors P3 NaOH.
        // Percentages split the Mode 7 alkalinity correction between NaOH and Alk.
        // Kalk baseline/caps remain separate, and pH safety can still block NaOH.
        bool enabled = true;
        float dayNaohPct = 0.0f;
        float dayAlkPct = 100.0f;
        float nightNaohPct = 100.0f;
        float nightAlkPct = 0.0f;
        float naohMaxPh = 8.45f;
    } mode7Split;

    HistoryEntry* aiHistory = nullptr; 
    void applySafetyEnforcement(DosingPlan &p);
    void addBaselineDemand(DosingPlan &p, int mode);
    void applyNaohPhCaution(DosingPlan &p, float currentPh);
    void applyAbsoluteCaps(DosingPlan &p);
    void logPlanToPSRAM(DosingPlan p, int mode);
};

#endif