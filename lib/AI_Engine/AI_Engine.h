#ifndef AI_ENGINE_H
#define AI_ENGINE_H

#include <Arduino.h>

struct DosingPlan {
    double kalk; double afr; double alk; double cacl2; double naoh; double mg;
    bool active = false;
};

struct HistoryEntry {
    uint32_t timestamp;
    DosingPlan plan;
    int mode;
};

class AIEngine {
public:
    void calculateNextPlan(int mode, float consAlk, float consCa, float consMg, float currentPh,bool lightsActive);
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

    struct {
        //TODO this is where you change chemitry strength
        float dkhPerMlKalk = 0.0000085f, dkhPerMlAfr = 0.1f, dkhPerMlAlk = 0.15f, dkhPerMlNaoh = 0.00255f, mgPerMlMg = 5.0f, caPerMlCacl2 = 0.42f;
    } chem;
    
    struct {
        //float maxKalkDay = 2000.0, maxNaohDay = 50.0, maxAlkRisePerDay = 0.5; // mark
        float maxKalkDay = 35000.0f, maxNaohDay = 800.0f, maxAlkRisePerDay = 1.5f;  // large reef tuned
    } limits;

    HistoryEntry* aiHistory = nullptr; 
    void applySafetyEnforcement(DosingPlan &p);
    void addBaselineDemand(DosingPlan &p, int mode);
    void applyAbsoluteCaps(DosingPlan &p);
    void logPlanToPSRAM(DosingPlan p, int mode); // <--- This was missing!
};

#endif