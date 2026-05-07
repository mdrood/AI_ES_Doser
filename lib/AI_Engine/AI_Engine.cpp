#include "AI_Engine.h"

// Assuming a helper struct for chemical potencies (dKH per ml) exists in your config
// Example: chem.dkhPerMlAfr = 0.1; 

void AIEngine::calculateNextPlan(int mode, float consAlk, float consCa, float consMg, float currentPh, bool lightsActive) {
    DosingPlan next; // Temporary bucket for ml/day results
    
    // Clear old plan
    memset(&next, 0, sizeof(DosingPlan));
    // 1. Scale the consumption/deviation by the ACTUAL tank volume 
    // compared to a reference 300G (1135.6L) volume.
    float volumeScale = _tankVolumeLiters / 1135.6f;
    
    float metabolicScalar = lightsActive ? 1.20f : 0.80f;
    
    // Applying both volume and metabolic scaling
    float adjAlk = consAlk * metabolicScalar * volumeScale;
    float adjCa  = consCa * metabolicScalar * volumeScale;
    float adjMg  = consMg * metabolicScalar * volumeScale;



    switch(mode) {
        case 1: // Mode 1: Kalkwasser Only
            next.kalk = adjAlk / chem.dkhPerMlKalk;
            break;

        case 2: // Mode 2: All For Reef (AFR) Only
            next.afr = adjAlk / chem.dkhPerMlAfr;
            break;

        case 3: // Mode 3: Kalk + AFR + Mg
            {
                float kalkRatio = (currentPh < 8.2) ? 0.75 : 0.4;
                next.kalk = (adjAlk * kalkRatio) / chem.dkhPerMlKalk;
                next.afr  = (adjAlk * (1.0 - kalkRatio)) / chem.dkhPerMlAfr;
                next.mg   = consMg / chem.mgPerMlMg;
            }
            break;

        case 4: // Mode 4: 3-Part (Alk, Ca, Mg)
            next.alk  = adjAlk / chem.dkhPerMlAlk;
            next.cacl2 =  adjCa/ chem.caPerMlCacl2;
            next.mg    = consMg / chem.mgPerMlMg;
            break;

        case 5: // Mode 5: Kalk + Alk + Ca + Mg
            {
                // Kalk handles the "Base Load" (approx 50%), Alk handles the gap
                float baseLoad = 0.5; 
                next.kalk = (adjAlk * baseLoad) / chem.dkhPerMlKalk;
                next.alk  = (adjAlk * (1.0 - baseLoad)) / chem.dkhPerMlAlk;
                next.cacl2 = adjCa / chem.caPerMlCacl2;
                next.mg    = consMg / chem.mgPerMlMg;
            }
            break;

        case 6: // Mode 6: Precision pH Mode (Kalk + CaCl2 + NaOH + Mg)
            if (currentPh < 8.15) {
                // Low pH: Prioritize NaOH (Sodium Hydroxide)
                next.naoh = (adjAlk * 0.85) / chem.dkhPerMlNaoh;
                next.kalk = (adjAlk * 0.15) / chem.dkhPerMlKalk;
            } else {
                // High pH: Shift back to Kalkwasser
                next.kalk = (adjAlk * 0.8) / chem.dkhPerMlKalk;
                next.naoh = (adjAlk * 0.2) / chem.dkhPerMlNaoh;
            }
            next.cacl2 = adjCa / chem.caPerMlCacl2;
            next.mg    = consMg / chem.mgPerMlMg;
            break;
    }

    // --- The "Expert" Safety Layer ---
    // First limit only the correction amount, then add known daily baseline demand.
    // This lets large reefs keep their normal consumption baseline while the AI
    // still limits how aggressively it corrects parameter errors.
    applySafetyEnforcement(next);
    addBaselineDemand(next, mode);
    applyAbsoluteCaps(next);

    // Clamp negative dosing to zero. A parameter above target should not create
    // negative bucket volume or subtract from future real dosing.
    if (next.kalk  < 0.0f) next.kalk  = 0.0f;
    if (next.afr   < 0.0f) next.afr   = 0.0f;
    if (next.alk   < 0.0f) next.alk   = 0.0f;
    if (next.cacl2 < 0.0f) next.cacl2 = 0.0f;
    if (next.naoh  < 0.0f) next.naoh  = 0.0f;
    if (next.mg    < 0.0f) next.mg    = 0.0f;

    // --- Commit to Global State ---
    currentPlan = next;
    logPlanToPSRAM(next, mode);
}

void AIEngine::applySafetyEnforcement(DosingPlan &p) {
    // Correction-only Delta-Max Check.
    // Baseline demand is added after this function, so known daily consumption
    // is not crushed by the correction safety layer.
    float predictedRise = (p.kalk * chem.dkhPerMlKalk) + (p.naoh * chem.dkhPerMlNaoh) + (p.alk * chem.dkhPerMlAlk);
    if (predictedRise > limits.maxAlkRisePerDay) {
        float scale = limits.maxAlkRisePerDay / predictedRise;
        p.kalk *= scale;
        p.naoh *= scale;
        p.alk *= scale;
        p.afr *= scale;
    }
}

void AIEngine::addBaselineDemand(DosingPlan &p, int mode) {
    switch (mode) {
        case 1:
            p.kalk += baselineKalkMlDay;
            break;
        case 2:
            // AFR-only mode has no separate kalk/CaCl2/NaOH baseline.
            break;
        case 3:
            p.kalk += baselineKalkMlDay;
            p.mg   += baselineMgMlDay;
            break;
        case 4:
            p.cacl2 += baselineCacl2MlDay;
            p.mg    += baselineMgMlDay;
            break;
        case 5:
            p.kalk  += baselineKalkMlDay;
            p.cacl2 += baselineCacl2MlDay;
            p.mg    += baselineMgMlDay;
            break;
        case 6:
            p.kalk  += baselineKalkMlDay;
            p.cacl2 += baselineCacl2MlDay;
            p.naoh  += baselineNaohMlDay;
            p.mg    += baselineMgMlDay;
            break;
        default:
            break;
    }
}

void AIEngine::applyAbsoluteCaps(DosingPlan &p) {
    // Absolute volumetric caps still protect against runaway commands after
    // baseline + correction have been combined.
    if (p.kalk > limits.maxKalkDay) p.kalk = limits.maxKalkDay;
    if (p.naoh > limits.maxNaohDay) p.naoh = limits.maxNaohDay;
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