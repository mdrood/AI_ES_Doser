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

    // Mg is a slow correction parameter. A normal low reading such as
    // 1320 ppm against a 1440 ppm target must never become tens of
    // thousands of mL/day. Ignore tiny/noisy Mg gaps and cap the daily
    // correction before baseline demand is added.
    float mgCorrectionMl = 0.0f;
    if (consMg > limits.mgDeadbandPpm && chem.mgPerMlMg > 0.0f) {
        mgCorrectionMl = adjMg / chem.mgPerMlMg;
        if (mgCorrectionMl > limits.maxMgCorrectionDay) {
            mgCorrectionMl = limits.maxMgCorrectionDay;
        }
    }

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
                next.mg   = mgCorrectionMl;
            }
            break;

        case 4: // Mode 4: 3-Part (Alk, Ca, Mg)
            next.alk  = adjAlk / chem.dkhPerMlAlk;
            next.cacl2 =  adjCa/ chem.caPerMlCacl2;
            next.mg    = mgCorrectionMl;
            break;

        case 5: // Mode 5: Kalk + Alk + Ca + Mg
            {
                // Kalk handles the "Base Load" (approx 50%), Alk handles the gap
                float baseLoad = 0.5; 
                next.kalk = (adjAlk * baseLoad) / chem.dkhPerMlKalk;
                next.alk  = (adjAlk * (1.0 - baseLoad)) / chem.dkhPerMlAlk;
                next.cacl2 = adjCa / chem.caPerMlCacl2;
                next.mg    = mgCorrectionMl;
            }
            break;

        case 6: // Mode 6: Kalk + CaCl2 + NaOH + Mg
            if (currentPh >= 8.45f) {
                // pH is already high. Do not create any NaOH correction.
                // Kalk baseline may remain, but NaOH baseline/correction is zeroed again
                // after baseline demand is added by applyNaohPhCaution().
                next.kalk = (adjAlk > 0.0f) ? (adjAlk / chem.dkhPerMlKalk) : 0.0f;
                next.naoh = 0.0f;
            } else if (currentPh < 8.15f) {
                // Low pH: prioritize NaOH for alk correction.
                next.naoh = (adjAlk * 0.85f) / chem.dkhPerMlNaoh;
                next.kalk = (adjAlk * 0.15f) / chem.dkhPerMlKalk;
            } else {
                // Normal pH: split correction between kalk and NaOH, then final pH
                // caution below tapers/blocks NaOH if pH is climbing.
                next.kalk = (adjAlk * 0.80f) / chem.dkhPerMlKalk;
                next.naoh = (adjAlk * 0.20f) / chem.dkhPerMlNaoh;
            }
            next.cacl2 = adjCa / chem.caPerMlCacl2;
            next.mg    = mgCorrectionMl;
            break;

        case 7: // Mode 7: Kalk + CaCl2 + NaOH + Alk on Pump 4
            // Eric hybrid mode:
            //   P1 = Kalk
            //   P2 = CaCl2
            //   P3 = NaOH
            //   P4 = Alk solution using the old Mg pump
            //
            // IMPORTANT MODE 7 FIX:
            // High pH should block/taper NaOH, but it must NOT shut off Kalk.
            // Eric still uses Kalk as a normal daily baseline/consumption path.
            // The dedicated Alk pump handles the extra alk correction when pH is high.
            if (currentPh >= 8.45f) {
                // High pH: protect pH by blocking NaOH correction.
                // Keep a small Kalk correction alive; baseline Kalk is added below.
                // Alk pump carries the majority of alk recovery.
                next.kalk = (adjAlk > 0.0f) ? ((adjAlk * 0.20f) / chem.dkhPerMlKalk) : 0.0f;
                next.naoh = 0.0f;
                next.alk  = (adjAlk > 0.0f) ? ((adjAlk * 0.80f) / chem.dkhPerMlAlk) : 0.0f;
            } else if (currentPh < 8.15f) {
                // Low pH: use NaOH/Kalk to help pH, but keep some Alk
                // correction on P4 so the tank can catch up even if NaOH later
                // gets tapered by pH safety.
                next.naoh = (adjAlk * 0.60f) / chem.dkhPerMlNaoh;
                next.kalk = (adjAlk * 0.20f) / chem.dkhPerMlKalk;
                next.alk  = (adjAlk * 0.20f) / chem.dkhPerMlAlk;
            } else {
                // Normal pH: balanced split. Alk pump carries the majority of
                // the correction so Mode 7 does not get stuck when pH rises.
                next.kalk = (adjAlk * 0.30f) / chem.dkhPerMlKalk;
                next.naoh = (adjAlk * 0.20f) / chem.dkhPerMlNaoh;
                next.alk  = (adjAlk * 0.50f) / chem.dkhPerMlAlk;
            }
            next.cacl2 = adjCa / chem.caPerMlCacl2;
            next.mg    = 0.0f; // Pump 4 is Alk in Mode 7, not Mg.
            break;
    }

    // --- The "Expert" Safety Layer ---
    // First limit only the correction amount, then add known daily baseline demand.
    // This lets large reefs keep their normal consumption baseline while the AI
    // still limits how aggressively it corrects parameter errors.
    applySafetyEnforcement(next);
    addBaselineDemand(next, mode);

    // Adaptive Mode-6 Alk recovery assist.
    // Mode 6 has no separate carbonate/bicarbonate Alk pump: low Alk is corrected
    // by Kalk + NaOH, while CaCl2 follows calcium. If Alk stays low and pH is
    // safe, slowly add a small NaOH baseline assist so the engine can actually
    // catch up instead of holding the same weak plan forever. This is intentionally
    // slow and is still protected by the pH caution layer and maxNaohDay cap below.
    static float adaptiveNaohBoostMlDay = 0.0f;
    static uint8_t lowAlkSafePhStreak = 0;

    if (mode == 6 && currentPh > 0.0f) {
        const bool alkVeryLow = (consAlk >= 1.20f);   // example: target 8.5, Alk <= 7.3
        const bool alkLow     = (consAlk >= 0.80f);   // example: target 8.5, Alk <= 7.7
        const bool phSafe     = (currentPh < 8.30f);
        const bool phCaution  = (currentPh >= 8.35f);

        if (alkVeryLow && phSafe) {
            if (lowAlkSafePhStreak < 12) lowAlkSafePhStreak++;
            if (lowAlkSafePhStreak >= 2) {
                adaptiveNaohBoostMlDay += 25.0f;      // gentle: +25 ml/day per AI cycle
            }
        } else if (!alkLow || phCaution) {
            lowAlkSafePhStreak = 0;
            adaptiveNaohBoostMlDay -= phCaution ? 75.0f : 25.0f;
        }

        if (adaptiveNaohBoostMlDay < 0.0f) adaptiveNaohBoostMlDay = 0.0f;
        if (adaptiveNaohBoostMlDay > 400.0f) adaptiveNaohBoostMlDay = 400.0f;

        next.naoh += adaptiveNaohBoostMlDay;
    }

    // pH-based NaOH caution layer.
    // This applies AFTER baseline demand is added so Eric's known daily NaOH
    // baseline is also reduced when pH is already high. Kalk is left alone.
    applyNaohPhCaution(next, currentPh);

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
        case 7:
            p.kalk  += baselineKalkMlDay;
            p.cacl2 += baselineCacl2MlDay;
            p.naoh  += baselineNaohMlDay;
            // In Mode 7 the physical Mg pump is repurposed as Alk.
            // Reuse the existing Mg baseline setting as P4 Alk baseline so
            // no new API/config field is required.
            p.alk   += baselineMgMlDay;
            break;
        default:
            break;
    }
}

void AIEngine::applyNaohPhCaution(DosingPlan &p, float currentPh) {
    // NaOH is useful for Alk correction, but it has a strong pH-raising effect.
    // When pH is already high, taper NaOH down instead of letting baseline +
    // correction keep pushing pH higher.
    //
    // Behavior:
    //   pH < 8.30  = normal NaOH
    //   8.30-8.35  = 80% NaOH
    //   8.35-8.40  = 60% NaOH
    //   8.40-8.45  = 35% NaOH
    //   >= 8.45    = 0% NaOH
    //
    // If pH is missing/invalid (0 or negative), do not apply a pH gate.
    if (currentPh <= 0.0f) return;

    if (currentPh >= 8.45f) {
        p.naoh = 0.0f;
    } else if (currentPh >= 8.40f) {
        p.naoh *= 0.35f;
    } else if (currentPh >= 8.35f) {
        p.naoh *= 0.60f;
    } else if (currentPh >= 8.30f) {
        p.naoh *= 0.80f;
    }
}

void AIEngine::applyAbsoluteCaps(DosingPlan &p) {
    // Absolute volumetric caps still protect against runaway commands after
    // baseline + correction have been combined.
    if (p.kalk > limits.maxKalkDay) p.kalk = limits.maxKalkDay;
    if (p.naoh > limits.maxNaohDay) p.naoh = limits.maxNaohDay;
    if (p.alk  > limits.maxAlkDay)  p.alk  = limits.maxAlkDay;
    if (p.mg > limits.maxMgDay) p.mg = limits.maxMgDay;
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