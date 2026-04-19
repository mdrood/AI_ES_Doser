#include "AI_Engine.h"

// Assuming a helper struct for chemical potencies (dKH per ml) exists in your config
// Example: chem.dkhPerMlAfr = 0.1; 

void AIEngine::calculateNextPlan(int mode, float consAlk, float consCa, float consMg, float currentPh) {
    DosingPlan next; // Temporary bucket for ml/day results
    
    // Clear old plan
    memset(&next, 0, sizeof(DosingPlan));

    switch(mode) {
        case 1: // Mode 1: Kalkwasser Only
            next.kalk = consAlk / chem.dkhPerMlKalk;
            break;

        case 2: // Mode 2: All For Reef (AFR) Only
            next.afr = consAlk / chem.dkhPerMlAfr;
            break;

        case 3: // Mode 3: Kalk + AFR + Mg
            {
                float kalkRatio = (currentPh < 8.2) ? 0.75 : 0.4;
                next.kalk = (consAlk * kalkRatio) / chem.dkhPerMlKalk;
                next.afr  = (consAlk * (1.0 - kalkRatio)) / chem.dkhPerMlAfr;
                next.mg   = consMg / chem.mgPerMlMg;
            }
            break;

        case 4: // Mode 4: 3-Part (Alk, Ca, Mg)
            next.alk  = consAlk / chem.dkhPerMlAlk;
            next.cacl2 = consCa / chem.caPerMlCacl2;
            next.mg    = consMg / chem.mgPerMlMg;
            break;

        case 5: // Mode 5: Kalk + Alk + Ca + Mg
            {
                // Kalk handles the "Base Load" (approx 50%), Alk handles the gap
                float baseLoad = 0.5; 
                next.kalk = (consAlk * baseLoad) / chem.dkhPerMlKalk;
                next.alk  = (consAlk * (1.0 - baseLoad)) / chem.dkhPerMlAlk;
                next.cacl2 = consCa / chem.caPerMlCacl2;
                next.mg    = consMg / chem.mgPerMlMg;
            }
            break;

        case 6: // Mode 6: Precision pH Mode (Kalk + CaCl2 + NaOH + Mg)
            if (currentPh < 8.15) {
                // Low pH: Prioritize NaOH (Sodium Hydroxide)
                next.naoh = (consAlk * 0.85) / chem.dkhPerMlNaoh;
                next.kalk = (consAlk * 0.15) / chem.dkhPerMlKalk;
            } else {
                // High pH: Shift back to Kalkwasser
                next.kalk = (consAlk * 0.8) / chem.dkhPerMlKalk;
                next.naoh = (consAlk * 0.2) / chem.dkhPerMlNaoh;
            }
            next.cacl2 = consCa / chem.caPerMlCacl2;
            next.mg    = consMg / chem.mgPerMlMg;
            break;
    }

    // --- The "Expert" Safety Layer ---
    applySafetyEnforcement(next);

    // --- Commit to Global State ---
    currentPlan = next;
    logPlanToPSRAM(next, mode);
}

void AIEngine::applySafetyEnforcement(DosingPlan &p) {
    // 1. Absolute Volumetric Cap (Prevents pump runaway)
    if (p.kalk > limits.maxKalkDay) p.kalk = limits.maxKalkDay;
    if (p.naoh > limits.maxNaohDay) p.naoh = limits.maxNaohDay;
    
    // 2. The "Delta-Max" Check
    // If calculated dose would raise Alk > 0.5 dKH/day, scale the whole plan down
    float predictedRise = (p.kalk * chem.dkhPerMlKalk) + (p.naoh * chem.dkhPerMlNaoh) + (p.alk * chem.dkhPerMlAlk);
    if (predictedRise > 0.5) {
        float scale = 0.5 / predictedRise;
        p.kalk *= scale;
        p.naoh *= scale;
        p.alk *= scale;
        p.afr *= scale;
    }
}

void AIEngine::logPlanToPSRAM(DosingPlan p, int mode) {
    if (aiHistory == nullptr) {
        // This actually uses your 8MB PSRAM
        aiHistory = (HistoryEntry*)ps_malloc(sizeof(HistoryEntry) * 100);
    }

    static int logIndex = 0;
    aiHistory[logIndex].timestamp = millis() / 1000; 
    aiHistory[logIndex].plan = p;
    aiHistory[logIndex].mode = mode;

    logIndex = (logIndex + 1) % 100;
}