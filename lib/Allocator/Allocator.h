#pragma once
#include "AI_EngineV2.h"

// =============================================================================
// §5.1 The Allocator is a Constrained Optimization Problem, Not an If/Else Tree
//
// v1's bug this directly fixes: "layered override logic where a later
// 'rescue' rule can silently defeat an earlier, customer-configured setting"
// (Mode 7 day/night split vs. NaOH rescue floor, AI_Engine.cpp lines ~202-354
// in the legacy file). That bug is structural — it's what happens when
// "priority" is expressed as "runs later in the function." v2 replaces all
// of it with ONE solve, ONE precedence order:
//
//   1. Build the desired-correction vector from the Kalman filters (§4.5) —
//      "how much of each parameter needs to move today."
//   2. Build the weighted cross-effect matrix: each chemical's stoichiometric
//      potency (ChemicalDeclaration::potencyPerMl), scaled down by
//      (a) its per-cell learned confidence (§5.2) and
//      (b) its continuous pH-derate weight if phSensitive and lights/pH
//          conditions call for caution (§5.1 "continuous weighting" +
//          "day/night routing is an always-available allocator input").
//   3. Solve non-negative least squares for per-chemical mL/day, bounded
//      above by each chemical's maxMlPerDay (§7 hard cap — never loosened).
//   4. Scale the *whole* solution down (uniformly) if the resulting predicted
//      parameter rise would exceed the maturity-arc-gated max-rise-per-day
//      cap (§4.4/§7) — this is the ONLY place scaling happens, so nothing
//      downstream can silently re-inflate a value the safety layer reduced.
//
// There is no step 5. No rescue floor, no later function that can reopen
// a decision step 3/4 already made. That is the precedence order.
// =============================================================================

class Allocator {
public:
    // Populates `plan` in place. Returns false (with plan zeroed) if the
    // sufficiency check fails — i.e. the declared chemical set physically
    // cannot reach target within safety caps (§5.2 "sufficiency check"),
    // which must surface to the customer as a real warning, not a silent
    // partial dose.
    static bool solve(
        const ChemicalDeclaration chemicals[],
        int numChemicals,
        const float desiredCorrectionPerDay[kNumParams],   // from Kalman, §4.5
        const SafetyEnvelope& safety,
        bool lightsActive,
        float currentPh,
        DosingPlanV2& plan
    );

    // §5.1 "continuous pH-impact weighting instead of a hard cutoff."
    // Returns 1.0 (no derate) well below the ceiling, tapering smoothly to
    // 0.0 at/above it, instead of v1's single-point naohMaxPh step.
    // ceilingPh is customer/system-recommended (Recommendable elsewhere);
    // the taper band is fixed at 0.30 dKH-equivalent-pH width here as a
    // reasonable smoothing default — exact band width is not specified in
    // the spec and should be confirmed, see MIGRATION_NOTES.md.
    static float phDerateWeight(float currentPh, float ceilingPh);

private:
    // Small active-set NNLS (Lawson-Hanson) solver, sized for the spec's
    // stated dimensionality ("4 parameters x perhaps 6-8 chemicals", §5.1).
    // No external matrix library required at this scale.
    static void nnls(
        const float A[kNumParams][kMaxChemicals],  // weighted cross-effect matrix
        const float b[kNumParams],                  // desired correction vector
        const float upperBound[kMaxChemicals],      // per-chemical mL/day cap
        int n,                                       // number of chemicals
        float x[kMaxChemicals]                       // output: mL/day per chemical
    );
};
