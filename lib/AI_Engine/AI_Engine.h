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
    void calculateNextPlan(int mode, float consAlk, float consCa, float consMg, float currentPh);
    void update(); // Wrapper for main.cpp
    DosingPlan currentPlan;

private:
    struct {
        float dkhPerMlKalk = 0.04, dkhPerMlAfr = 0.1, dkhPerMlAlk = 0.15, dkhPerMlNaoh = 0.25, mgPerMlMg = 5.0, caPerMlCacl2 = 2.0;
    } chem;
    
    struct {
        float maxKalkDay = 2000.0, maxNaohDay = 50.0, maxAlkRisePerDay = 0.5;
    } limits;

    HistoryEntry* aiHistory = nullptr; 
    void applySafetyEnforcement(DosingPlan &p);
    void logPlanToPSRAM(DosingPlan p, int mode); // <--- This was missing!
};

#endif