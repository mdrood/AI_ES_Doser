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
            // Adaptive Mode 7 split:
            // The older fixed split could starve P4 Alk when Eric's alkalinity
            // stayed low. This version shifts more correction onto the dedicated
            // Alk pump as the alk deficit grows, while still protecting pH.
            {
                float kalkShare = 0.30f;
                float naohShare = 0.20f;
                float alkShare  = 0.50f;

                if (consAlk >= 1.20f) {
                    // Very low alk: P4 Alk carries most of the recovery.
                    kalkShare = 0.15f;
                    naohShare = 0.10f;
                    alkShare  = 0.75f;
                } else if (consAlk >= 0.80f) {
                    // Low alk: bias recovery toward P4 Alk.
                    kalkShare = 0.20f;
                    naohShare = 0.15f;
                    alkShare  = 0.65f;
                }

                if (currentPh >= 8.60f) {
                    // Dangerous/high pH: block NaOH and move that correction to P4 Alk.
                    alkShare += naohShare;
                    naohShare = 0.0f;
                    if (kalkShare > 0.20f) kalkShare = 0.20f;
                    alkShare = 1.0f - kalkShare;
                } else if (currentPh >= 8.45f) {
                    // Eric recovery rule: if Alk is still badly low, do NOT fully
                    // shut off Pump 3 just because pH is 8.45-8.59. Eric proved
                    // NaOH is the fast alk recovery tool. Keep some NaOH unless
                    // pH reaches the hard 8.60 cutoff.
                    if (consAlk >= 1.20f) {
                        kalkShare = 0.10f;
                        naohShare = 0.35f;
                        alkShare  = 0.55f;
                    } else if (consAlk >= 0.80f) {
                        kalkShare = 0.15f;
                        naohShare = 0.25f;
                        alkShare  = 0.60f;
                    } else {
                        // Alk is not far behind; use P4 Alk instead of NaOH at high-ish pH.
                        alkShare += naohShare;
                        naohShare = 0.0f;
                        if (kalkShare > 0.20f) kalkShare = 0.20f;
                        alkShare = 1.0f - kalkShare;
                    }
                } else if (currentPh > 0.0f && currentPh < 8.15f) {
                    // Low pH: keep NaOH useful, but do not let it starve P4 Alk
                    // when Alk is seriously behind.
                    if (consAlk >= 1.20f) {
                        // Very low Alk + low pH:
                        // keep NaOH helping pH, but push most recovery to P4 Alk.
                        kalkShare = 0.05f;
                        naohShare = 0.25f;
                        alkShare  = 0.70f;
                    } else if (consAlk >= 0.80f) {
                        // Low Alk + low pH:
                        // still bias toward P4 Alk so Eric can catch up.
                        kalkShare = 0.10f;
                        naohShare = 0.30f;
                        alkShare  = 0.60f;
                    } else {
                        kalkShare = 0.20f;
                        naohShare = 0.60f;
                        alkShare  = 0.20f;
                    }
                }


                // ===== EVENING / NIGHT pH ASSIST (Mode 7 only) =====
                // Lights OFF + Alk behind:
                // start helping earlier in the evening before pH crashes.
                // This does not create extra total correction here; it shifts
                // more of the existing correction toward Pump 3 (NaOH).
                if (!lightsActive && currentPh > 0.0f && consAlk >= 0.40f) {
                    float nightBoost = 1.0f;

                    if (currentPh < 7.90f && consAlk >= 0.80f) {
                        nightBoost = 1.75f;   // severe overnight pH sag + low Alk
                    } else if (currentPh < 8.05f && consAlk >= 0.80f) {
                        nightBoost = 1.50f;   // night assist, stronger
                    } else if (currentPh < 8.25f && consAlk >= 0.60f) {
                        nightBoost = 1.25f;   // evening assist before the crash
                    }

                    if (nightBoost > 1.0f) {
                        naohShare *= nightBoost;

                        float total = kalkShare + naohShare + alkShare;
                        if (total > 0.0f) {
                            kalkShare /= total;
                            naohShare /= total;
                            alkShare  /= total;
                        }

                        Serial.printf(
                            "[NIGHT PH ASSIST] lights=off pH=%.2f alkGap=%.2f boost=%.2f\n",
                            currentPh,
                            consAlk,
                            nightBoost
                        );
                    }
                }

                next.kalk = (adjAlk > 0.0f && chem.dkhPerMlKalk > 0.0f) ? ((adjAlk * kalkShare) / chem.dkhPerMlKalk) : 0.0f;
                next.naoh = (adjAlk > 0.0f && chem.dkhPerMlNaoh > 0.0f) ? ((adjAlk * naohShare) / chem.dkhPerMlNaoh) : 0.0f;
                next.alk  = (adjAlk > 0.0f && chem.dkhPerMlAlk  > 0.0f) ? ((adjAlk * alkShare)  / chem.dkhPerMlAlk)  : 0.0f;
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

    // Adaptive Mode-7 P4 Alk demand-learning assist.
    // This is different from the instant correction split above. It watches the
    // alk trend over time and learns extra daily P4 Alk demand when Eric's tank
    // keeps falling even while the normal plan is dosing.
    static float mode7P4AlkAssistMlDay = 0.0f;
    static float lastMode7ConsAlk = 0.0f;
    static bool hasMode7TrendSample = false;
    static unsigned long lastMode7AssistAdjustMs = 0;

    if (mode == 7) {
        const unsigned long MODE7_ASSIST_INTERVAL_MS = 60UL * 60UL * 1000UL; // adjust at most hourly
        const float MODE7_ASSIST_STEP_UP_ML_DAY = 75.0f;
        const float MODE7_ASSIST_STEP_DOWN_ML_DAY = 150.0f;
        const float MODE7_ASSIST_MAX_ML_DAY = 1000.0f;
        const float MODE7_LOW_ALK_GAP_DKH = 0.60f;
        const float MODE7_NEAR_TARGET_GAP_DKH = 0.25f;
        const float MODE7_NOT_IMPROVING_DKH = -0.05f; // consAlk must drop by >0.05 to count as improving
        const float MODE7_FAST_IMPROVING_DKH = -0.20f;

        unsigned long nowMs = millis();

        if (!hasMode7TrendSample) {
            hasMode7TrendSample = true;
            lastMode7ConsAlk = consAlk;
            lastMode7AssistAdjustMs = nowMs;

            // Give very low alk an initial gentle assist immediately after boot/flash
            // instead of waiting a full hour. This is still clamped below.
            if (consAlk >= 1.20f) {
                mode7P4AlkAssistMlDay = 100.0f;
            }
        } else if (nowMs - lastMode7AssistAdjustMs >= MODE7_ASSIST_INTERVAL_MS) {
            float deltaGap = consAlk - lastMode7ConsAlk;

            if (consAlk >= MODE7_LOW_ALK_GAP_DKH && deltaGap > MODE7_NOT_IMPROVING_DKH) {
                // Alk is still low and has not clearly improved over the last hour.
                mode7P4AlkAssistMlDay += MODE7_ASSIST_STEP_UP_ML_DAY;
            } else if (consAlk <= MODE7_NEAR_TARGET_GAP_DKH || deltaGap <= MODE7_FAST_IMPROVING_DKH) {
                // Near target or improving quickly: back off faster than we ramp up.
                mode7P4AlkAssistMlDay -= MODE7_ASSIST_STEP_DOWN_ML_DAY;
            }

            if (mode7P4AlkAssistMlDay < 0.0f) mode7P4AlkAssistMlDay = 0.0f;
            if (mode7P4AlkAssistMlDay > MODE7_ASSIST_MAX_ML_DAY) mode7P4AlkAssistMlDay = MODE7_ASSIST_MAX_ML_DAY;

            Serial.printf("MODE7 ALK LEARN: gap=%.2f prevGap=%.2f delta=%.2f assist=%.2f ml/day\n",
                          consAlk, lastMode7ConsAlk, deltaGap, mode7P4AlkAssistMlDay);

            lastMode7ConsAlk = consAlk;
            lastMode7AssistAdjustMs = nowMs;
        }

        if (mode7P4AlkAssistMlDay > 0.0f) {
            next.alk += mode7P4AlkAssistMlDay;
        }
    }


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

    // Mode 7 NaOH rescue for Eric:
    // If Alk is low AND pH is low, Pump 3 must be allowed to help harder.
    // The previous floor often left Eric stuck at naoh/day=450 even with
    // Alk around 7.4-7.5 and pH in the 7.8s. This floor is still protected
    // by maxNaohDay in applyAbsoluteCaps(), and still blocks at pH >= 8.60.
    if (mode == 7 && consAlk >= 0.40f && currentPh > 0.0f && currentPh < 8.60f) {
        float minNaohRecoveryMlDay = 0.0f;

        if (currentPh < 7.90f && consAlk >= 0.80f) {
            minNaohRecoveryMlDay = 1100.0f;   // severe low pH + low Alk
        } else if (currentPh < 8.00f && consAlk >= 0.80f) {
            minNaohRecoveryMlDay = 900.0f;    // low pH + low Alk
        } else if (currentPh < 8.10f && consAlk >= 0.80f) {
            minNaohRecoveryMlDay = 750.0f;    // early recovery support
        } else if (consAlk >= 1.20f) {
            minNaohRecoveryMlDay = 650.0f;    // very low Alk even if pH is not low
        } else if (consAlk >= 0.80f) {
            minNaohRecoveryMlDay = 450.0f;    // normal low-Alk floor
        }

        // At 8.45-8.59, still allow NaOH for low-Alk recovery, but do not
        // push as hard as when pH is low/normal.
        if (currentPh >= 8.45f) {
            minNaohRecoveryMlDay *= 0.75f;
        }

        if (minNaohRecoveryMlDay > 0.0f && next.naoh < minNaohRecoveryMlDay) {
            next.naoh = minNaohRecoveryMlDay;
            Serial.printf("MODE7 NAOH RESCUE: gap=%.2f pH=%.2f floor=%.2f naoh=%.2f ml/day\n",
                          consAlk, currentPh, minNaohRecoveryMlDay, next.naoh);
        }
    }

    // Mode 7 recovery guard for Eric:
    // If Alk and/or pH are low, do NOT let the adaptive split reduce Kalk.
    // Kalk is Eric's steady pH support, so recovery mode keeps it pinned at
    // the configured max while the learned P4 Alk assist adds catch-up dose.
    if (mode == 7) {
        const bool mode7Recovery = (consAlk >= 0.40f) || (currentPh > 0.0f && currentPh < 8.10f);
        if (mode7Recovery && next.kalk < limits.maxKalkDay) {
            next.kalk = limits.maxKalkDay;
            Serial.printf("MODE7 RECOVERY: Kalk locked at %.2f ml/day (gap=%.2f pH=%.2f)\n",
                          next.kalk, consAlk, currentPh);
        }
    }

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
    // Global behavior:
    //   pH < 8.30  = normal NaOH
    //   8.30-8.35  = 80% NaOH
    //   8.35-8.40  = 60% NaOH
    //   8.40-8.45  = 35% NaOH
    //   >= 8.45    = 0% NaOH
    // Mode 7 can restore NaOH immediately after this layer when Alk is
    // badly low and pH is below the hard 8.60 cutoff.
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