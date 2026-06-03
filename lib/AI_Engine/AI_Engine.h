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
        // Mark (300g) Active
        //float dkhPerMlKalk = 0.0000085f, dkhPerMlAfr = 0.00015f, dkhPerMlAlk = 0.00015f, dkhPerMlNaoh = 0.00255f, mgPerMlMg = 0.0050f, caPerMlCacl2 = 0.42f;
        // Eric (1100g) Inactive
         float dkhPerMlKalk = 0.0000046f, dkhPerMlAfr = 0.000082f, dkhPerMlAlk = 0.00126f, dkhPerMlNaoh = 0.00139f, mgPerMlMg = 0.00273f, caPerMlCacl2 = 0.229f;
    } chem;

    // ====== SAFETY LIMITS ======
    struct {
        // Mark (300g) Active
        /*float maxKalkDay = 2500.0f;
        float maxNaohDay = 100.0f;
        float maxAlkRisePerDay = 0.5f;

        // Magnesium safety rails. Mg changes slowly, so the AI should never
        // convert a ppm gap directly into massive daily dosing. For Mark's
        // 300g system, 100 mL/day is a conservative correction cap.
        float maxMgCorrectionDay = 100.0f;
        float maxMgDay = 50.0f;
        float mgDeadbandPpm = 25.0f;*/

        // Eric large reef tuned example (Inactive):
        float maxKalkDay = 35000.0f, maxNaohDay = 400.0f, maxAlkDay = 1000.0f, maxAlkRisePerDay = 1.5f;
        float maxMgCorrectionDay = 250.0f, maxMgDay = 250.0f, mgDeadbandPpm = 25.0f;
    } limits;

    HistoryEntry* aiHistory = nullptr; 
    void applySafetyEnforcement(DosingPlan &p);
    void addBaselineDemand(DosingPlan &p, int mode);
    void applyNaohPhCaution(DosingPlan &p, float currentPh);
    void applyAbsoluteCaps(DosingPlan &p);
    void logPlanToPSRAM(DosingPlan p, int mode);
};

#endif