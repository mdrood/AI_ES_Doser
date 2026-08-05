#include "Allocator.h"
#include <string.h>
#include "logger.h"

// -----------------------------------------------------------------------
// Tiny linear algebra helpers, sized only for kNumParams (4) x kMaxChemicals
// (8). Gaussian elimination with partial pivoting on the normal equations
// of whatever "passive set" (currently-unconstrained variables) NNLS is
// working on. This is intentionally simple, not a general linear algebra
// library — the spec explicitly calls for "~100 lines of C++, no heavy
// library needed given the tiny dimensionality" (§5.1).
// -----------------------------------------------------------------------
namespace {

// Solves the small symmetric linear system (AtA) x = Atb for the given
// passive-set indices only. `k` is the number of passive indices.
void solvePassiveSet(
    const float A[kNumParams][kMaxChemicals],
    const float b[kNumParams],
    const int passive[kMaxChemicals],
    int k,
    float xOut[kMaxChemicals]
) {
    if (k == 0) return;

    float AtA[kMaxChemicals][kMaxChemicals] = {{0}};
    float Atb[kMaxChemicals] = {0};

    for (int i = 0; i < k; i++) {
        int ci = passive[i];
        for (int j = 0; j < k; j++) {
            int cj = passive[j];
            float s = 0.0f;
            for (int r = 0; r < kNumParams; r++) s += A[r][ci] * A[r][cj];
            AtA[i][j] = s;
        }
        float s = 0.0f;
        for (int r = 0; r < kNumParams; r++) s += A[r][ci] * b[r];
        Atb[i] = s;
    }

    // Gaussian elimination with partial pivoting, k is at most kMaxChemicals.
    float M[kMaxChemicals][kMaxChemicals + 1];
    for (int i = 0; i < k; i++) {
        for (int j = 0; j < k; j++) M[i][j] = AtA[i][j];
        M[i][k] = Atb[i];
    }
    for (int col = 0; col < k; col++) {
        int pivot = col;
        float best = fabsf(M[col][col]);
        for (int r = col + 1; r < k; r++) {
            if (fabsf(M[r][col]) > best) { best = fabsf(M[r][col]); pivot = r; }
        }
        if (best < 1e-9f) continue; // singular-ish direction; leave as 0
        if (pivot != col) {
            for (int c = 0; c <= k; c++) { float t = M[col][c]; M[col][c] = M[pivot][c]; M[pivot][c] = t; }
        }
        for (int r = 0; r < k; r++) {
            if (r == col) continue;
            float factor = M[r][col] / M[col][col];
            for (int c = col; c <= k; c++) M[r][c] -= factor * M[col][c];
        }
    }
    for (int i = 0; i < k; i++) {
        float diag = M[i][i];
        int ci = passive[i];
        xOut[ci] = (fabsf(diag) < 1e-9f) ? 0.0f : (M[i][k] / diag);
    }
}

} // namespace

float ::Allocator::phDerateWeight(float currentPh, float ceilingPh) {
    if (currentPh <= 0.0f) return 1.0f; // missing/invalid reading: no gate (matches v1 behavior)

    // Resolved 2026-07-23 — not a fleet-data question like the coral-load
    // multipliers; this is fully derivable from numbers the spec already
    // states. §1's pH target range tops out at 8.40; this function's
    // caller passes 8.60 as its DEFAULT ceiling (customer-configurable
    // since 2026-07-24, v1: mode7NaohMaxPh, see SafetyEnvelope) — a 0.20
    // gap between "top of healthy target" and "hard stop" in the default
    // case. Setting the taper band to 1.5x that gap (0.30) means throttling
    // begins 0.30 below whatever ceiling is in effect — inside the target
    // range in the default case, well before the ceiling — rather than
    // waiting until the tank is outside its target band to start easing
    // off. If the target-range/default-ceiling numbers ever change,
    // re-derive from the same ratio rather than picking a new constant by
    // feel; a customer-configured ceiling shifts where the taper starts
    // without needing this band re-derived.
    const float band = 0.30f;
    float startTaper = ceilingPh - band;
    if (currentPh <= startTaper) return 1.0f;
    if (currentPh >= ceilingPh) return 0.0f;
    // Smooth cosine taper rather than a linear ramp — avoids a slope
    // discontinuity right at startTaper.
    float t = (currentPh - startTaper) / band; // 0..1
    return 0.5f * (1.0f + cosf(t * PI));
}

void ::Allocator::nnls(
    const float A[kNumParams][kMaxChemicals],
    const float b[kNumParams],
    const float upperBound[kMaxChemicals],
    int n,
    float x[kMaxChemicals]
) {
    // Lawson-Hanson style active-set NNLS, bounded above by upperBound
    // (implemented as: solve unconstrained on the passive set, clip to
    // [0, upperBound], demote any variable that clips to a bound out of
    // the passive set). Small fixed iteration cap — dimensionality is
    // tiny (<=8), this converges in a handful of passes in practice.
    bool inPassive[kMaxChemicals] = {false};
    // Fixed 2026-08-04 (second bug in this function, found after the first
    // fix above still left ALK at x=0 on a real reefDoser3 log despite
    // having real unmet demand and a nonzero A_col untouched by Calcium's
    // saturation). Demoting a bound-violating variable out of inPassive[]
    // stops it from re-entering the joint solve -- but it does NOT stop it
    // from being re-picked as "next candidate to promote," since the
    // promotion loop below only checks inPassive[i], and a capped variable
    // looks identical to a never-tried one under that check. A chemical
    // whose demand is far larger than its cap keeps having the largest
    // leftover residual/gradient every single iteration, so it wins
    // re-selection every time and burns all 20 iterations being
    // re-confirmed as capped -- starving every other chemical of ever
    // getting a turn, even ones with real, satisfiable demand. This second
    // array permanently locks a variable out of both the joint solve AND
    // promotion candidacy for the rest of this solve, once it has hit
    // either bound -- proper bounded-least-squares semantics.
    bool fixedAtBound[kMaxChemicals] = {false};
    for (int i = 0; i < n; i++) x[i] = 0.0f;

    for (int iter = 0; iter < 20; iter++) {
        int passive[kMaxChemicals];
        int k = 0;
        for (int i = 0; i < n; i++) if (inPassive[i]) passive[k++] = i;

        float xTry[kMaxChemicals];
        memcpy(xTry, x, sizeof(xTry));
        solvePassiveSet(A, b, passive, k, xTry);

        bool changed = false;
        // Clip passive-set solution into bounds; demote violators on EITHER
        // bound. Fixed 2026-08-04: this used to only demote on the lower
        // bound (x < 0) and merely clip-in-place on the upper bound,
        // leaving an upper-bound-saturated variable "free" forever. Every
        // later iteration's solvePassiveSet then re-solved that variable
        // jointly and unconstrained alongside any newly-promoted candidate
        // -- easily driving the newcomer negative and demoting IT back to
        // zero instead, even with real unmet demand and untouched capacity
        // sitting right there. Confirmed against a real reefDoser12 log:
        // Kalkwasser pinned at its (correct, newly-fixed-potency) cap while
        // Calcium/Alkalinity/NaOH sat at x=0.000 indefinitely with
        // sufficient=no. Demoting on both bounds and pinning x[ci] to
        // exactly the bound it hit means it's a fixed constant in the
        // residual (line ~140 below) from here on, not a variable that
        // keeps re-entering the joint solve.
        for (int idx = 0; idx < k; idx++) {
            int ci = passive[idx];
            if (xTry[ci] < 0.0f) {
                inPassive[ci] = false; fixedAtBound[ci] = true; x[ci] = 0.0f; changed = true;
            } else if (xTry[ci] > upperBound[ci]) {
                inPassive[ci] = false; fixedAtBound[ci] = true; x[ci] = upperBound[ci]; changed = true;
            } else {
                x[ci] = xTry[ci];
            }
        }

        // Compute residual gradient for inactive variables; bring in the
        // most-negative-residual (best improving) inactive variable.
        float residual[kNumParams];
        for (int r = 0; r < kNumParams; r++) {
            float pred = 0.0f;
            for (int i = 0; i < n; i++) pred += A[r][i] * x[i];
            residual[r] = b[r] - pred;
        }

        int bestIdx = -1;
        float bestGrad = 1e-6f; // threshold: only bring in genuinely improving vars
        for (int i = 0; i < n; i++) {
            if (inPassive[i] || fixedAtBound[i] || upperBound[i] <= 0.0f) continue;
            float grad = 0.0f;
            for (int r = 0; r < kNumParams; r++) grad += A[r][i] * residual[r];
            if (grad > bestGrad) { bestGrad = grad; bestIdx = i; }
        }

        if (bestIdx < 0 && !changed) break; // converged: nothing improving, nothing violating
        if (bestIdx >= 0) inPassive[bestIdx] = true;
    }

    for (int i = 0; i < n; i++) x[i] = constrain(x[i], 0.0f, upperBound[i]);
}

bool ::Allocator::solve(
    const ChemicalDeclaration chemicals[],
    int numChemicals,
    const float desiredCorrectionPerDay[kNumParams],
    const SafetyEnvelope& safety,
    bool lightsActive,
    float currentPh,
    DosingPlanV2& plan
) {
    memset(&plan, 0, sizeof(DosingPlanV2));
    plan.numChemicals = numChemicals;
    if (numChemicals < 0 || numChemicals > kMaxChemicals) return false;

    // Fixed 2026-07-28: this used to unconditionally reject numChemicals==0
    // with a hard `return false` before any sufficiency logic ran at all --
    // a SEPARATE bug from the one already fixed in removeChemical()'s own
    // desired[] computation, with the identical visible symptom (blocking
    // removal of a customer's last/only chemical even on a device that had
    // never taken a single measurement). Zero chemicals is only a real
    // problem if something is actually desired; if nothing is, it's a
    // trivially correct empty plan, same principle as the sufficiency loop
    // below already uses for a single unaddressable parameter.
    if (numChemicals == 0) {
        bool anythingDesired = false;
        for (int p = 0; p < kNumParams; p++) {
            if (desiredCorrectionPerDay[p] > 0.01f) { anythingDesired = true; break; }
        }
        return !anythingDesired;
    }

    // --- Step 2: weighted cross-effect matrix -----------------------------
    float A[kNumParams][kMaxChemicals] = {{0}};
    float upperBound[kMaxChemicals] = {0};

    // §5: "day/night (light-cycle) aware routing ... an always-available
    // input to the allocator (current light state + pH)" — folded directly
    // into the matrix weights used by the ONE solve, not applied afterward.
    for (int c = 0; c < numChemicals; c++) {
        const ChemicalDeclaration& chem = chemicals[c];
        if (!chem.active) { upperBound[c] = 0.0f; continue; }
        upperBound[c] = chem.maxMlPerDay; // §7 hard cap, unmodified by learning

        float phWeight = 1.0f;
        if (chem.phSensitive) {
            // Resolved 2026-07-24: ceiling now comes from the caller's
            // SafetyEnvelope (§5.1's customer-configurable value, v1:
            // mode7NaohMaxPh) instead of being hardcoded here. 0.0 means
            // "caller hasn't configured this yet" (Recommendable's own
            // zero-init default) -- fall back to the same 8.60 this
            // function used unconditionally before, rather than gating
            // every pH-sensitive chemical to zero strength.
            float ceilingPh = safety.naohPhCeiling.value;
            if (ceilingPh <= 0.0f) ceilingPh = 8.60f;
            phWeight = phDerateWeight(currentPh, ceilingPh);
            // Lights-off assist: v1's proven "start helping earlier in the
            // evening" behavior (§5), expressed as a smooth boost rather
            // than the old multiplicative nightBoost hack, still bounded
            // by the same pH taper above so it can never defeat the gate.
            if (!lightsActive) phWeight = fminf(1.0f, phWeight * 1.15f);

            // Added 2026-07-28: the two mechanisms above only ever engage
            // near the pH ceiling -- they do nothing when pH is well within
            // range, which is exactly when a customer might still want a
            // chemical avoided during the day (e.g. Eric's proven-working
            // V1 config: no NaOH in daylight hours, full stop, regardless
            // of current pH). daytimeSuppressPercent is independent of
            // ceiling proximity by design -- a direct, customer-set
            // preference rather than a reactive safety response.
            if (lightsActive && chem.daytimeSuppressPercent > 0.0f) {
                float suppress = chem.daytimeSuppressPercent / 100.0f;
                if (suppress > 1.0f) suppress = 1.0f;
                phWeight *= (1.0f - suppress);
            }
        }

        for (int p = 0; p < kNumParams; p++) {
            A[p][c] = chem.potencyPerMl[p] * chem.confidence[p] * phWeight;
        }
    }

    // --- Step 1: desired correction vector already provided by caller -----
    // (Kalman filters, §4.5, live in the math-engine layer above this call.)

    // --- Step 3: NNLS solve ------------------------------------------------
    float x[kMaxChemicals] = {0};
    nnls(A, desiredCorrectionPerDay, upperBound, numChemicals, x);

    // TEMP DIAGNOSTIC (2026-08-02): only real numbers can settle whether
    // Kalk is legitimately out-competing CaCl2/NaOH/Alk in the NNLS solve
    // or something else is zeroing them structurally -- a hand-built
    // simulation with guessed potency/desired values did NOT reproduce
    // production's behavior, so guessing further isn't useful. Remove once
    // root cause is confirmed from real output.
    //
    // Paired with logger.printf (not Serial-only) to match every other log
    // line in this codebase -- the logs actually being captured/reviewed
    // come through the LittleFS/Firebase logger pipeline, not a live
    // serial session, so a Serial-only print here would silently never
    // show up in what gets pasted back for diagnosis.
    Serial.println("--- ALLOCATOR DIAGNOSTIC ---");
    logger.println("--- ALLOCATOR DIAGNOSTIC ---");
    for (int c = 0; c < numChemicals; c++) {
        Serial.printf("  chem[%d] name=%s active=%d maxMlPerDay=%.2f potency(Alk,pH,Ca,Mg)=(%.6f,%.6f,%.6f,%.6f) confidence=(%.2f,%.2f,%.2f,%.2f) A_col=(%.6f,%.6f,%.6f,%.6f) x=%.3f\n",
            c, chemicals[c].name, chemicals[c].active, chemicals[c].maxMlPerDay,
            chemicals[c].potencyPerMl[0], chemicals[c].potencyPerMl[1], chemicals[c].potencyPerMl[2], chemicals[c].potencyPerMl[3],
            chemicals[c].confidence[0], chemicals[c].confidence[1], chemicals[c].confidence[2], chemicals[c].confidence[3],
            A[0][c], A[1][c], A[2][c], A[3][c],
            x[c]);
        logger.printf("  chem[%d] name=%s active=%d maxMlPerDay=%.2f potency(Alk,pH,Ca,Mg)=(%.6f,%.6f,%.6f,%.6f) confidence=(%.2f,%.2f,%.2f,%.2f) A_col=(%.6f,%.6f,%.6f,%.6f) x=%.3f\n",
            c, chemicals[c].name, chemicals[c].active, chemicals[c].maxMlPerDay,
            chemicals[c].potencyPerMl[0], chemicals[c].potencyPerMl[1], chemicals[c].potencyPerMl[2], chemicals[c].potencyPerMl[3],
            chemicals[c].confidence[0], chemicals[c].confidence[1], chemicals[c].confidence[2], chemicals[c].confidence[3],
            A[0][c], A[1][c], A[2][c], A[3][c],
            x[c]);
    }
    Serial.printf("  desired(Alk,pH,Ca,Mg)=(%.4f,%.4f,%.4f,%.4f)\n",
        desiredCorrectionPerDay[0], desiredCorrectionPerDay[1], desiredCorrectionPerDay[2], desiredCorrectionPerDay[3]);
    logger.printf("  desired(Alk,pH,Ca,Mg)=(%.4f,%.4f,%.4f,%.4f)\n",
        desiredCorrectionPerDay[0], desiredCorrectionPerDay[1], desiredCorrectionPerDay[2], desiredCorrectionPerDay[3]);
    Serial.println("--- END ALLOCATOR DIAGNOSTIC ---");
    logger.println("--- END ALLOCATOR DIAGNOSTIC ---");

    float achieved[kNumParams] = {0};
    for (int p = 0; p < kNumParams; p++)
        for (int c = 0; c < numChemicals; c++)
            achieved[p] += A[p][c] * x[c];

    // --- §5.2 sufficiency check --------------------------------------------
    // If even at full allowed dose the achievable correction falls well
    // short of what's desired, this must surface as a customer-facing
    // warning, not a silently-partial dose.
    //
    // Fixed 2026-07-24: a parameter with ZERO active chemicals able to touch
    // it at all (e.g. Mg in a hardware mode with no Mg pump wired) is a
    // structural fact about the current chemical/pump configuration, not a
    // capacity shortfall — achieved[p] is unconditionally 0 in that case
    // regardless of how well-supplied every other parameter is, and would
    // otherwise mark `sufficient=false` on every single cycle even when
    // Alk/Ca dosing is working perfectly. That drowns out the genuine
    // warning this check exists for (§5.2: "insufficient chemical capacity
    // for current targets" should mean "add more of what you have," not
    // "you have zero ability to touch this parameter at all, permanently,
    // until you rewire hardware"). Both cases are reported, but distinctly.
    bool anyChemicalTouches[kNumParams] = {false};
    for (int p = 0; p < kNumParams; p++) {
        for (int c = 0; c < numChemicals; c++) {
            if (chemicals[c].active && A[p][c] != 0.0f) { anyChemicalTouches[p] = true; break; }
        }
    }

    bool sufficient = true;
    bool anyUnaddressableParam = false;
    for (int p = 0; p < kNumParams; p++) {
        if (desiredCorrectionPerDay[p] <= 0.01f) continue;
        if (!anyChemicalTouches[p]) {
            // Structural: no active chemical can move this parameter at
            // all. Not folded into `sufficient` -- that flag is reserved
            // for "you have relevant chemicals but not enough of them."
            anyUnaddressableParam = true;
            continue;
        }
        if (achieved[p] < 0.5f * desiredCorrectionPerDay[p]) {
            sufficient = false;
        }
    }

    // --- Step 4: single uniform safety-cap scale-down (the ONLY scaling
    // step — nothing downstream is allowed to re-touch this) ---------------
    float caps[kNumParams] = {
        safety.maxAlkRisePerDayDkh.value,
        safety.maxPhRisePerDay.value,
        safety.maxCaRisePerDayPpm.value,
        safety.maxMgRisePerDayPpm.value
    };
    float scale = 1.0f;
    for (int p = 0; p < kNumParams; p++) {
        if (caps[p] > 0.0f && achieved[p] > caps[p]) {
            float s = caps[p] / achieved[p];
            if (s < scale) scale = s;
        }
    }
    for (int c = 0; c < numChemicals; c++) plan.mlPerDay[c] = x[c] * scale;

    snprintf(plan.explanation, sizeof(plan.explanation),
        "Allocator: %d chemicals, safety-scale=%.2f, sufficient=%s, pH=%.2f lights=%s%s",
        numChemicals, scale, sufficient ? "yes" : "no", currentPh,
        lightsActive ? "on" : "off",
        anyUnaddressableParam
            ? " [NOTE: at least one parameter has no active chemical able to touch it -- structural, not a dosing shortfall]"
            : "");

    return sufficient;
}
