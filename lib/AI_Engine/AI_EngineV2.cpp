#include "AI_EngineV2.h"
#include "Allocator.h"
#include <string.h>
#include <Preferences.h>
#include <cmath>

// =============================================================================
// §6/§7 Shared tank estimator
// =============================================================================
namespace TankEstimator {

float scale(const TankProfile& tank, float referenceValueAtModerateLoad, Purpose purpose) {
    if (tank.tankVolumeLiters <= 0.0f) return referenceValueAtModerateLoad;

    float volumeScale = tank.tankVolumeLiters / TankProfile::kReferenceVolumeLiters;

    // Coral/livestock load step multiplier. Two tables, not one — see the
    // Purpose comment in AI_EngineV2.h for why baseline and cap can't share
    // a number even though §6/§7's Core Design Principle uses "the same
    // estimator mechanism" (same *shape* of function, deliberately
    // different tuning per purpose since only one of them self-corrects).
    // Neither table is confirmed fleet data yet — §10 leaves "exact
    // formula/multiplier still needs definition" open; revisit once
    // reefDoser fleet data (§9) exists to validate against.
    float loadScale;
    if (purpose == Purpose::SafetyCap) {
        // Conservative/tight on purpose (owner decision, 2026-07-23): a cap
        // is never adjusted by the learning system (§7), so it doesn't get
        // a second chance to be right. Compressed spread vs. baseline —
        // errs toward limiting max dosing speed on heavy/SPS tanks rather
        // than risking an over-generous cap with no self-correction.
        switch (tank.coralLoad) {
            case CoralLoad::Light:       loadScale = 0.75f; break;
            case CoralLoad::Moderate:    loadScale = 1.00f; break;
            case CoralLoad::Heavy:       loadScale = 1.20f; break;
            case CoralLoad::SPSDominant: loadScale = 1.50f; break;
            default:                     loadScale = 1.00f; break;
        }
    } else {
        // Baseline demand estimate — corrected by the Kalman learner (§4.5)
        // as real per-tank data accumulates (§8 Observation/Break-in
        // Period), so this can afford to be a wider, more realistic spread.
        // SPS-dominant tanks are known to consume meaningfully more Alk/Ca
        // per gallon than a lightly-stocked tank.
        switch (tank.coralLoad) {
            case CoralLoad::Light:       loadScale = 0.60f; break;
            case CoralLoad::Moderate:    loadScale = 1.00f; break;
            case CoralLoad::Heavy:       loadScale = 1.40f; break;
            case CoralLoad::SPSDominant: loadScale = 2.20f; break;
            default:                     loadScale = 1.00f; break;
        }
    }

    return referenceValueAtModerateLoad * volumeScale * loadScale;
}

} // namespace TankEstimator

// =============================================================================
// §4.5 Math engine — scalar constant-velocity Kalman filter, one per
// water parameter. Stage 1 of the spec's recommended staged build.
// =============================================================================
namespace MathEngine {

void predict(ParamKalmanState& s, float dtDays, float processNoise) {
    if (!s.initialized) return; // nothing to predict until first measurement
    // Constant-velocity model: level += trend * dt
    s.level += s.trend * dtDays;

    // Covariance propagation for a 2-state constant-velocity model:
    //   P = F P F^T + Q,  F = [[1, dt], [0, 1]]
    float pLevel = s.pLevel + dtDays * (s.pCross + s.pCross + dtDays * s.pTrend);
    float pCross = s.pCross + dtDays * s.pTrend;
    float pTrend = s.pTrend;

    s.pLevel = pLevel + processNoise * dtDays;
    s.pCross = pCross;
    s.pTrend = pTrend + processNoise * dtDays * 0.1f; // trend drifts slower than level
}

void update(ParamKalmanState& s, float measurement, float measurementNoise) {
    if (!s.initialized) {
        s.level = measurement;
        s.trend = 0.0f;
        s.pLevel = measurementNoise; // start uncertain, shrinks with data (§4.4)
        // Fixed 2026-07-31: was seeded equal to measurementNoise, the same
        // magnitude as pLevel's uncertainty -- but these are different
        // units. pLevel's uncertainty is in absolute dKH/ppm; pTrend's is
        // in dKH/ppm PER DAY, a rate. Seeding a rate's uncertainty at full
        // absolute-value magnitude was never dimensionally right,
        // independent of the separate processNoiseFraction issue. This
        // mattered in practice because pTrend barely shrinks via update()
        // until pCross (level-trend covariance) has built up from several
        // spaced-out real measurements -- pCross starts at zero, so early
        // on pTrend sits near this initial seed for a long time, and
        // predict()'s covariance-propagation term (P = F P F^T, see the
        // dtDays*(2*pCross + dtDays*pTrend) line above) uses that large
        // pTrend directly. A large seed here was quietly injecting far
        // more growth into pLevel each predict() call than the tuned
        // processNoiseFraction term alone -- the actual cause of maturity
        // decaying roughly 55x faster than intended on a real device.
        // 0.1x is a reasoned starting scale-down (assumes day-to-day trend
        // uncertainty is meaningfully smaller than a full measurement's
        // worth of absolute uncertainty), not fleet-validated -- same
        // caveat as processNoiseFraction itself.
        s.pTrend = measurementNoise * 0.1f;
        s.pCross = 0.0f;
        s.initialPLevel = measurementNoise; // scale reference for maturity() -- see AI_EngineV2.h
        s.bestPLevel = measurementNoise;    // ratchet starts here, only ever improves from now on
        s.initialized = true;
        return;
    }

    // Standard 2-state Kalman update, measuring level only (H = [1, 0]).
    float innovation = measurement - s.level;              // §4.4 anomaly signal
    float sInnov = s.pLevel + measurementNoise;             // innovation covariance
    if (sInnov <= 0.0f) sInnov = 1e-6f;

    float kLevel = s.pLevel / sInnov;
    float kTrend = s.pCross / sInnov;

    s.level += kLevel * innovation;
    s.trend += kTrend * innovation;

    float pLevelNew = (1.0f - kLevel) * s.pLevel;
    float pCrossNew = (1.0f - kLevel) * s.pCross;
    float pTrendNew = s.pTrend - kTrend * s.pCross;

    s.pLevel = pLevelNew;
    s.pCross = pCrossNew;
    s.pTrend = pTrendNew;

    // Ratchet: only ever moves toward "more proven," never regresses just
    // because pLevel happened to grow again since the best point (that
    // growth is predict()'s job, not this function's -- see
    // ParamKalmanState::bestPLevel and provenMaturity()).
    if (s.bestPLevel < 0.0f || pLevelNew < s.bestPLevel) {
        s.bestPLevel = pLevelNew;
    }
}

float measurementNoiseFor(WaterParam param, MeasurementSource source) {
    // Resolved 2026-07-23. Previously this took only `source` and returned
    // one flat noise value applied to whichever parameter was being
    // updated — a real unit-scale bug, not just an unconfirmed magnitude:
    // Alk lives on a ~8 dKH scale, pH ~8.2, Ca ~450 ppm, Mg ~1400 ppm, so
    // one flat number was badly wrong-sized for at least three of the four.
    //
    // Magnitudes below are grounded in well-established reef test-kit
    // precision characteristics — Trident's automated titration and Hanna's
    // digital colorimeters are consistently tighter than hand titration or
    // color-match kits, and Mg's manual titration endpoint is the hobby's
    // most notoriously imprecise common test. Still not fleet-calibrated
    // against this product's actual sensors/kits (§10 "test-method
    // disagreement" remains open for that final tuning), but the *shape*
    // — correct units per parameter, consistent Trident < Hanna < manual
    // ordering within each — is now right, not just the ordering alone.
    static const float kNoise[kNumParams][3] = {
        // {ApexTrident, HannaChecker, ManualTest}
        /* P_ALK */ {0.05f, 0.08f, 0.15f},   // dKH
        /* P_PH  */ {0.02f, 0.05f, 0.10f},   // pH units — color-match kits are coarse
        /* P_CA  */ {5.0f,  8.0f,  15.0f},   // ppm
        /* P_MG  */ {10.0f, 15.0f, 30.0f},   // ppm — worst manual precision of the four
    };
    int col = (source == MeasurementSource::ApexTrident)  ? 0
            : (source == MeasurementSource::HannaChecker) ? 1
                                                            : 2; // ManualTest / default
    return kNoise[param][col];
}

float maxCorrectionStep(const ParamKalmanState& s, float youngTankMax, float matureTankMax) {
    // §4.4: maturity throttles correction size directly, not just test
    // interval — falls out of the filter's own covariance. Low maturity
    // (new tank) -> youngTankMax; high maturity (~3mo+) -> matureTankMax.
    float m = s.maturity();
    return youngTankMax * (1.0f - m) + matureTankMax * m;
}

float expectedDiurnalPhDelta(bool lightsActive, float dtDays) {
    // §3.5.3: simple two-state rate model -- see the header declaration's
    // comment for the full reasoning and caveats (not fleet-validated,
    // not a smooth continuous curve, only light-state-aware not
    // time-within-cycle-aware). Assumes a roughly even ~12h lights-on /
    // ~12h lights-off split contributing the full assumed daily swing in
    // each direction.
    const float kAssumedDailySwingPh = 0.15f;
    const float kAssumedHalfCycleDays = 0.5f; // 12 hours
    float ratePerDay = kAssumedDailySwingPh / kAssumedHalfCycleDays;
    return (lightsActive ? ratePerDay : -ratePerDay) * dtDays;
}

} // namespace MathEngine

namespace {

// Added 2026-08-04: implements §2's missing feedback arrow and the second
// half of §4.1's Day-layer reconciliation (the first half -- extrapolation
// and predict/update -- was already the existing Kalman filter; this is
// "compare predicted vs. actual... feeds back into learning"). Called from
// ingestMeasurement() BEFORE MathEngine::update() consumes the innovation,
// since that's the one place "predicted vs. actual" is available before
// the filter moves on.
//
// Deliberately a simple, bounded heuristic for this first version (matches
// §4.5's own "recommended staged build -- start simpler" philosophy) --
// not a full Bayesian per-cell update. What it does:
//   1. If nothing was actually dosed for this parameter since the last
//      real measurement (totalContribution ~= 0), there is no basis to
//      attribute anything -- skip entirely. A chemical that wasn't used
//      can't have its confidence judged by an outcome it didn't cause.
//   2. Compute ratio = actualChange / totalContribution -- how well the
//      REAL response matched what the currently-declared confidence
//      predicted it should be. ~1.0 means the model was right; >1 means
//      the chemical(s) worked BETTER than currently trusted (confidence
//      should rise); <1 means worse (confidence should fall).
//   3. Guard against wild ratios (outside kMinRatio..kMaxRatio) -- a
//      hugely mismatched ratio is far more likely to mean something
//      external happened (an unlogged water change, a bioload shift, a
//      bad reading) than "this chemical's true potency is 10x different
//      than declared." §3.5 exists specifically to prevent exactly this
//      kind of misattribution corrupting the learning signal; since event
//      logging (§3.5) isn't implemented yet, this guard is the interim
//      safeguard -- skip the adjustment rather than risk learning the
//      wrong thing from an unexplained external event.
//   4. For each chemical that contributed, nudge its confidence toward
//      confidence*ratio, weighted by that chemical's SHARE of the total
//      contribution (a chemical responsible for 10% of the expected
//      effect shouldn't be blamed/credited for the other 90%) and by a
//      learning rate that shrinks as the filter matures -- matches §4.4's
//      governing principle throughout the rest of this file: early on,
//      larger adjustments are expected and appropriate; a mature filter
//      "already knows the plan," so new data should refine, not swing.
//   5. Clamp to [kMinConfidence, kMaxConfidence] -- never fully zero (a
//      chemical with real declared potency always keeps SOME prior, per
//      §5.1's stoichiometric cold-start philosophy) and never fully 1.0
//      (keeps a small margin of humility, consistent with 0.35's own
//      cold-start reasoning never asserting certainty).
// Added 2026-08-04: return type changed from void to bool -- true
// specifically means "the ratio guard rejected this outcome," which is
// exactly §4.4's anomaly case ("a miss bigger than the mature-phase
// correction cap would allow gets flagged as an anomaly to the customer").
// That case used to just silently skip learning with nothing surfaced to
// anyone; now the caller (ingestMeasurement) can set a real, checkable
// flag from it. False covers every other case, including "nothing was
// dosed, nothing to judge" -- that's not an anomaly, just no basis to
// evaluate one at all.
bool updateConfidenceFromOutcome(
    ChemicalDeclaration chemicals[],
    int numChemicals,
    float pendingContribution[][kNumParams],
    WaterParam p,
    float predictedLevel,
    float measurement,
    float maturity,
    float knownExternalEffect,   // §3.5.3: e.g. pH's expected diurnal swing over this window -- subtracted before judging chemical effectiveness
    float ratioToleranceMultiplier // §3.5.2: widened during a post-livestock-addition window so an expected demand shift isn't misread as an anomaly
) {
    float totalContribution = 0.0f;
    for (int c = 0; c < numChemicals; c++) totalContribution += pendingContribution[c][p];

    // Reset unconditionally on the way out -- this window is closing
    // regardless of whether an adjustment ends up applying below.
    struct ResetGuard {
        float (*arr)[kNumParams];
        int n;
        WaterParam param;
        ~ResetGuard() { for (int c = 0; c < n; c++) arr[c][param] = 0.0f; }
    } resetGuard{pendingContribution, numChemicals, p};

    const float kMinContribution = 1e-4f; // effectively "nothing was dosed" for this parameter's units
    if (fabsf(totalContribution) < kMinContribution) return false;

    // §3.5.3: back out any known non-chemical contribution (currently only
    // pH's diurnal swing, see caller) before judging chemical effectiveness
    // -- otherwise a natural day/night pH oscillation gets misattributed
    // as "the chemical worked better/worse than expected."
    float actualChange = (measurement - knownExternalEffect) - predictedLevel;
    float ratio = actualChange / totalContribution;

    // §3.5.2: widened post-livestock-addition, since a real demand shift in
    // that window is expected, not a chemical-effectiveness anomaly.
    const float kMinRatio = 0.3f / ratioToleranceMultiplier;
    const float kMaxRatio = 3.0f * ratioToleranceMultiplier;
    if (!isfinite(ratio) || ratio < kMinRatio || ratio > kMaxRatio) return true; // §4.4 anomaly: likely external event, not chemical feedback

    const float kBaseLearningRate = 0.15f; // unvalidated starting magnitude, same caveat as other §10 constants in this file
    float learningRate = kBaseLearningRate * (1.0f - maturity);

    for (int c = 0; c < numChemicals; c++) {
        if (pendingContribution[c][p] == 0.0f) continue; // this chemical wasn't part of this window's dosing
        float share = pendingContribution[c][p] / totalContribution;
        float oldConfidence = chemicals[c].confidence[p];
        float newConfidence = oldConfidence * (1.0f + learningRate * share * (ratio - 1.0f));

        const float kMinConfidence = 0.05f;
        const float kMaxConfidence = 1.00f;
        chemicals[c].confidence[p] = constrain(newConfidence, kMinConfidence, kMaxConfidence);
    }
    return false;
}

} // namespace



DosingPlanV2 AIEngineV2::recalculate(bool lightsActive, float currentPh) {
    // Added 2026-08-04: cached so ingestMeasurement()'s §3.5.3 diurnal pH
    // correction has access to the current light state without changing
    // ingestMeasurement()'s own signature (would touch every call site in
    // main.cpp). recalculate() already receives fresh light state every
    // cycle, so this stays reasonably current between real measurements.
    lastKnownLightsActive = lightsActive;

    // --- desired correction vector, one entry per water parameter --------
    // Each parameter's gap-to-target, converted to a per-day correction
    // request and capped by that filter's own maturity-gated step size
    // (§4.4) — this single rule replaces v1's bespoke two-speed
    // fastAlk/adaptiveNaohBoost ladder for every parameter, not just Alk.
    float desired[kNumParams] = {0, 0, 0, 0};

    struct { Recommendable* lo; Recommendable* hi; float youngMax; float matureMax; } targets[kNumParams] = {
        // Fixed 2026-08-06: matureMax was 0.10f -- confirmed via direct
        // tracing (not the separate, much larger 2.00 dKH/day
        // SafetyEnvelope ceiling in Allocator.cpp, which this never came
        // close to using) as the actual reason a mature filter's Alk
        // correction was capped at ~0.13 dKH/day, closely matching five
        // days of a real tank sitting stuck around 7.0-7.1 dKH against an
        // 8.0-8.4 target. 0.35 is still well under that 2.00 ceiling, but
        // lets a mature, confident estimate request real daily progress
        // instead of this very conservative original default.
        { &targetAlkDkh, nullptr, 0.40f, 0.35f },   // §4.4 Alk: v1's fastAlk.maxBoostDkhDay-shaped bounds
        { &targetPhLow,  &targetPhHigh, 0.05f, 0.02f },
        { &targetCaPpm,  nullptr, 20.0f, 5.0f },
        { &targetMgPpm,  nullptr, 15.0f, 3.0f },
    };

    for (int p = 0; p < kNumParams; p++) {
        if (!filters[p].initialized) continue; // §8.5: no dosing before baseline test
        float targetLevel = targets[p].lo->value;
        if (targets[p].hi) targetLevel = (targets[p].lo->value + targets[p].hi->value) * 0.5f;

        float gap = targetLevel - filters[p].level; // positive = below target
        float step = MathEngine::maxCorrectionStep(filters[p], targets[p].youngMax, targets[p].matureMax);
        float gapCorrection = constrain(gap, -step, step);
        if (gapCorrection < 0.0f) gapCorrection = 0.0f; // allocator only adds, never removes (matches v1 clamp)

        // Fixed 2026-07-24, found via the §9.5 synthetic digital-twin
        // simulator: a steadily-consuming tank's gap naturally shrinks
        // toward ~0 as the filter's level estimate correctly tracks the
        // decline, even though true Alk keeps falling at the observed
        // rate -- gapCorrection alone can never counteract that once the
        // needed daily replenishment exceeds the young/mature step cap,
        // which exists to bound how fast an ABRUPT one-time correction
        // is allowed to happen (§4.4/§7 safety), not to throttle ordinary
        // steady-state maintenance dosing that just keeps the tank AT its
        // current level. This is exactly the "learned daily consumption"
        // replacement MIGRATION_NOTES.md describes v1's baseline-demand
        // store being replaced by ("v2's per-parameter Kalman filter is
        // meant to capture ongoing consumption automatically... WITHOUT a
        // separately maintained baseline number") -- the trend state was
        // being tracked correctly all along, just never wired into the
        // correction request. Deliberately NOT capped by the same step
        // limit as gapCorrection above: the overall ceiling on total
        // achieved rise-per-day still applies via SafetyEnvelope inside
        // Allocator::solve() (§7 "the ONE place a rise-per-day gets
        // capped") -- that remains the real safety backstop, not a
        // second cap duplicated here.
        float replenish = fmaxf(0.0f, -filters[p].trend);

        desired[p] = gapCorrection + replenish;
    }

    // Fixed 2026-08-03: pH is dosed additively just like Alk/Ca/Mg above, so
    // the "allocator only adds, never removes" clamp on gapCorrection is
    // physically correct for it too -- there's no such thing as requesting
    // a negative dose. But every chemical this product doses has a
    // non-negative pH potency (NaOH/soda ash/kalkwasher all push pH UP;
    // none pull it down), so when pH sits above its target band that clamp
    // produces desired[P_PH] == 0 every single cycle, indistinguishable from
    // "pH is fine." That fed straight into Allocator::solve's sufficiency
    // check (`if (desiredCorrectionPerDay[p] <= 0.01f) continue;`), which
    // then never evaluates pH at all -- so an out-of-band-high pH could sit
    // there indefinitely with sufficient=yes and no customer-facing signal,
    // missing both §1 (hold pH 8.0-8.4) and §5.2 (never fail silently).
    // Computed here, independently of the desired[] vector the Allocator
    // consumes, because this is a structural fact about the chemical
    // inventory (no chemical can lower pH), not a capacity shortfall the
    // Allocator's NNLS solve is equipped to reason about.
    bool phAboveTarget = filters[P_PH].initialized &&
                          targetPhHigh.value > 0.0f &&
                          filters[P_PH].level > targetPhHigh.value;

    DosingPlanV2 plan;
    bool sufficient = Allocator::solve(chemicals, numChemicals, desired, safety, lightsActive, currentPh, plan);
    if (!sufficient) {
        // §5.2: surface as a real warning, never a silent partial dose.
        // Wiring to the actual customer-facing alert channel is outside
        // this file's scope (Dashboard/WebRoutes own that) — this leaves
        // the signal in the explanation string and expects the caller to
        // check the return path in production, not swallow it here.
        strncat(plan.explanation, " [WARNING: insufficient chemical capacity for current targets]",
                sizeof(plan.explanation) - strlen(plan.explanation) - 1);
    }
    if (phAboveTarget) {
        // Same channel as the insufficiency warning above -- deliberately
        // independent of `sufficient`, since it can be true even on a
        // cycle where every other parameter is dosing fine.
        strncat(plan.explanation, " [WARNING: pH above target range and no available chemical can lower it]",
                sizeof(plan.explanation) - strlen(plan.explanation) - 1);
    }
    // Fixed 2026-07-24: remember this plan so advanceTime() can feed its
    // known effect into the next predict step (see advanceTime()'s
    // comment) — without this, trend estimation converges to a
    // permanently wrong partial-cancellation equilibrium.
    for (int c = 0; c < numChemicals; c++) lastMlPerDay[c] = plan.mlPerDay[c];
    return plan;
}

void AIEngineV2::ingestMeasurement(WaterParam p, float value, MeasurementSource source) {
    // §3.5.3: only pH has a modeled natural swing to back out. Every other
    // parameter's knownExternalEffect stays 0 -- no equivalent natural
    // oscillation is modeled for Alk/Ca/Mg in this version.
    float knownExternalEffect = 0.0f;
    if (p == P_PH) {
        knownExternalEffect = MathEngine::expectedDiurnalPhDelta(
            lastKnownLightsActive, daysSinceLastMeasurement[p]
        );
    }

    // §3.5.2: widen tolerance for ~2x the addition's expected transition
    // window (see logLivestockAddition()) rather than a hard cutoff --
    // matches the rest of this file's preference for graceful tapering
    // over abrupt gates.
    float ratioToleranceMultiplier = 1.0f;
    if (daysSinceLivestockAddition >= 0.0f && daysSinceLivestockAddition < 21.0f) {
        ratioToleranceMultiplier = 2.0f;
    }

    // Added 2026-08-04: cleared before the call (not after), so this
    // always reflects only the outcome of THIS measurement, never a
    // lingering flag from a previous one this function never touches.
    lastAnomaly[p] = updateConfidenceFromOutcome(
        chemicals, numChemicals, pendingContribution,
        p, filters[p].level, value, filters[p].maturity(),
        knownExternalEffect, ratioToleranceMultiplier
    );

    MathEngine::update(filters[p], value, MathEngine::measurementNoiseFor(p, source));

    // Added 2026-08-04, §4.2/§4.3: record this real measurement into the
    // Week/Month history buffer -- circular, overwrites the oldest slot
    // once full. Recorded AFTER the Kalman update so `value` here is the
    // actual raw measurement, not a filtered estimate -- the Week/Month
    // layers are explicitly meant to work from real logged data, per
    // §4.2's own framing ("tied to real test data, not extrapolation").
    {
        int slot = historyNextSlot[p];
        // Fixed 2026-08-04: brace-list assignment (history[p][slot] = {a,b,c})
        // compiled fine under the C++17 check used to verify this file
        // during development, but failed on the actual ESP32 toolchain --
        // confirmed real compiler error, not a false alarm: "no match for
        // operator=... no known conversion from brace-enclosed initializer
        // list." That syntax needs the type treated as a simple aggregate,
        // which apparently isn't guaranteed under whatever C++ standard
        // this toolchain compiles with for a struct with in-class default
        // member initializers. Assigning each field explicitly sidesteps
        // the whole question and works under any C++ standard version.
        history[p][slot].value = value;
        history[p][slot].recordedAtDay = totalElapsedDays;
        history[p][slot].valid = true;
        historyNextSlot[p] = (slot + 1) % kHistoryCapacity;
    }

    // This measurement closes the reconciliation window for this
    // parameter; the next one starts fresh.
    daysSinceLastMeasurement[p] = 0.0f;
}

namespace {
// Added 2026-08-04: shared by getWeekTrend/getMonthTrend below -- finds
// the oldest and newest real measurement still inside the trailing
// `windowDays` window and returns a simple two-point slope between them.
// Deliberately not a full regression across every point in the window --
// matches §4.5's own "start simpler" staged-build philosophy, same
// reasoning already used for the confidence-learning step earlier in
// this file. Returns false via foundEnough if fewer than 2 real
// measurements exist inside the window. Takes plain parallel arrays
// rather than AIEngineV2::HistoryEntry directly -- that struct is a
// private nested type, and there's no real need to expose it just for
// this helper to read it.
float windowTrend(const float values[], const float days[], const bool valid[], int capacity,
                   float nowDay, float windowDays, bool& foundEnough) {
    float cutoff = nowDay - windowDays;
    float oldestDay = 1e18f, oldestValue = 0.0f;
    float newestDay = -1e18f, newestValue = 0.0f;
    int countInWindow = 0;

    for (int i = 0; i < capacity; i++) {
        if (!valid[i] || days[i] < cutoff) continue;
        countInWindow++;
        if (days[i] < oldestDay) { oldestDay = days[i]; oldestValue = values[i]; }
        if (days[i] > newestDay) { newestDay = days[i]; newestValue = values[i]; }
    }

    foundEnough = (countInWindow >= 2) && (newestDay - oldestDay > 0.01f); // guard against a same-instant division
    if (!foundEnough) return 0.0f;
    return (newestValue - oldestValue) / (newestDay - oldestDay);
}
} // namespace

static void unpackHistory(const AIEngineV2::HistoryEntry hist[], int capacity,
                           float values[], float days[], bool valid[]) {
    for (int i = 0; i < capacity; i++) {
        values[i] = hist[i].value;
        days[i] = hist[i].recordedAtDay;
        valid[i] = hist[i].valid;
    }
}

float AIEngineV2::getWeekTrend(WaterParam p) const {
    float values[kHistoryCapacity], days[kHistoryCapacity]; bool valid[kHistoryCapacity];
    unpackHistory(history[p], kHistoryCapacity, values, days, valid);
    bool ok;
    return windowTrend(values, days, valid, kHistoryCapacity, totalElapsedDays, 7.0f, ok);
}

bool AIEngineV2::hasWeekData(WaterParam p) const {
    float values[kHistoryCapacity], days[kHistoryCapacity]; bool valid[kHistoryCapacity];
    unpackHistory(history[p], kHistoryCapacity, values, days, valid);
    bool ok;
    windowTrend(values, days, valid, kHistoryCapacity, totalElapsedDays, 7.0f, ok);
    return ok;
}

float AIEngineV2::getMonthTrend(WaterParam p) const {
    float values[kHistoryCapacity], days[kHistoryCapacity]; bool valid[kHistoryCapacity];
    unpackHistory(history[p], kHistoryCapacity, values, days, valid);
    bool ok;
    return windowTrend(values, days, valid, kHistoryCapacity, totalElapsedDays, 30.0f, ok);
}

bool AIEngineV2::hasMonthData(WaterParam p) const {
    float values[kHistoryCapacity], days[kHistoryCapacity]; bool valid[kHistoryCapacity];
    unpackHistory(history[p], kHistoryCapacity, values, days, valid);
    bool ok;
    windowTrend(values, days, valid, kHistoryCapacity, totalElapsedDays, 30.0f, ok);
    return ok;
}

float AIEngineV2::historySpanDays(WaterParam p) const {
    float oldestDay = 1e18f, newestDay = -1e18f;
    bool any = false;
    for (int i = 0; i < kHistoryCapacity; i++) {
        if (!history[p][i].valid) continue;
        any = true;
        if (history[p][i].recordedAtDay < oldestDay) oldestDay = history[p][i].recordedAtDay;
        if (history[p][i].recordedAtDay > newestDay) newestDay = history[p][i].recordedAtDay;
    }
    return any ? (newestDay - oldestDay) : 0.0f;
}

bool AIEngineV2::isDrifting(WaterParam p) const {
    if (!hasWeekData(p) || !hasMonthData(p)) return false; // not enough real history to judge either window yet
    float week = getWeekTrend(p);
    float month = getMonthTrend(p);

    // §4.3: a real, sustained divergence between the short and long
    // window, not any difference at all -- week and month trends will
    // almost never match exactly even with zero real drift, just from
    // ordinary measurement noise. kDriftThreshold is a reasoned starting
    // point (roughly: the week's pace needs to be at least double the
    // month's average pace, in the same direction, before calling it
    // real drift rather than noise) -- not fleet-validated, same caveat
    // as this file's other unvalidated constants.
    const float kDriftRatioThreshold = 2.0f;
    const float kMinMeaningfulMonthTrend = 1e-4f; // avoid a near-zero-denominator false positive
    if (fabsf(month) < kMinMeaningfulMonthTrend) return fabsf(week) > kMinMeaningfulMonthTrend * kDriftRatioThreshold;
    bool sameDirection = (week > 0) == (month > 0);
    return sameDirection && (fabsf(week) > fabsf(month) * kDriftRatioThreshold);
}

void AIEngineV2::advanceTime(float dtDays) {
    // Added 2026-08-04: monotonic clock for §4.2/§4.3's history buffer
    // timestamps -- see HistoryEntry's comment in the header. Increments
    // regardless of whether a real measurement follows, same as the two
    // trackers right below it.
    totalElapsedDays += dtDays;

    // Added 2026-08-04: feeds ingestMeasurement()'s §3.5.3 diurnal
    // correction and §3.5.2 event-window aging -- both need to know how
    // much time has actually elapsed, which this function already knows
    // every time it's called regardless of whether a measurement follows.
    for (int p = 0; p < kNumParams; p++) daysSinceLastMeasurement[p] += dtDays;
    ageEnvironmentalEvents(dtDays);

    // Fixed 2026-07-27: was a single flat 0.01 applied identically to all
    // four parameters, regardless of their wildly different natural scales
    // (Alk ~8 dKH vs Ca ~450 ppm vs Mg ~1400 ppm) -- the exact same class
    // of unit-scale bug already found and fixed for measurementNoiseFor(),
    // just missed here. Found investigating a real-tank report of maturity
    // dropping unexpectedly between manual tests: a flat absolute noise
    // term regrows pLevel far faster, in relative terms, for a small-scale
    // parameter like Alk than for a large-scale one like Ca, even though
    // both should legitimately become "stale" at a comparable RATE. Scaled
    // to a fraction of each parameter's own initialPLevel instead (same
    // "relative to where this parameter started" principle maturity()
    // itself already uses) so a day without new data affects all four
    // proportionally, not just the small-scale ones. Fraction (1%/day) is
    // still an unvalidated starting magnitude, not fleet-calibrated --
    // same caveat as before, just correctly shaped now instead of also
    // being wrong-sized.
    const float processNoiseFraction = 0.01f;
    for (int p = 0; p < kNumParams; p++) {
        float processNoise = processNoiseFraction * filters[p].initialPLevel;
        MathEngine::predict(filters[p], dtDays, processNoise);

        // Fixed 2026-07-24, found via the §9.5 synthetic digital-twin
        // simulator: without this, `trend` is a Kalman estimate of the
        // NET rate of change (true consumption minus whatever dose is
        // already running), not the gross consumption rate. Since the
        // dosing decision (recalculate()) asks for "enough to cancel
        // `trend`," and `trend` already reflects a partially-dosed
        // system, the loop settles into a stable but WRONG equilibrium
        // that only ever partially replenishes consumption — verified in
        // the simulator as Alk declining indefinitely at a constant
        // reduced rate instead of stabilizing at target. Fix: feed the
        // KNOWN effect of the last computed plan directly into the
        // predicted level (a proper Kalman filter with a known control
        // input, not a bug in the noise model) — this isolates whatever
        // the NEXT measurement's residual reflects to genuinely
        // unexplained consumption only, so `trend` converges to the true
        // demand rate instead of "true demand minus current dose."
        if (filters[p].initialized) {
            float doseEffect = 0.0f;
            for (int c = 0; c < numChemicals; c++) {
                float contribution = chemicals[c].potencyPerMl[p] * chemicals[c].confidence[p] * lastMlPerDay[c];
                doseEffect += contribution;
                // Added 2026-08-04: same per-chemical term already being
                // summed into doseEffect above -- just also kept per-
                // chemical instead of discarded, so ingestMeasurement()'s
                // confidence-learning step can later attribute a real
                // measurement's outcome back to the specific chemicals
                // responsible, not just the aggregate. See
                // pendingContribution's comment in AI_EngineV2.h.
                pendingContribution[c][p] += contribution * dtDays;
            }
            filters[p].level += doseEffect * dtDays;
        }
    }
}

void AIEngineV2::addChemical(const ChemicalDeclaration& chem) {
    if (numChemicals >= kMaxChemicals) return;
    chemicals[numChemicals] = chem;

    // §5.1 "Cold-start ... not from zero. The existing chemical-strength
    // entry ... is most of a cross-effect matrix already." §5.2 "only that
    // new row starts at low confidence" -- LOW, not zero. A caller that
    // declared a chemical's potency for a given parameter (potencyPerMl[p]
    // != 0) has already told us it's known-by-stoichiometry to affect that
    // parameter; the allocator (A[p][c] = potency * confidence * phWeight)
    // multiplies that potency by confidence, so leaving confidence at the
    // struct's default 0.0f makes the allocator treat every declared
    // chemical as inert on every parameter regardless of correct potency
    // or correct caps -- this was the actual cause of CaCl2/NaOH/Alk
    // permanently dosing 0.00 on reefDoser12 despite real Alk/Ca gaps and
    // correctly-set maxMlPerDay caps (confirmed against live serial log,
    // 2026-08-02). Only touches THIS chemical's row, only for parameters
    // it actually claims to affect -- confidence for unrelated cells, and
    // for every other already-declared chemical, is untouched, matching
    // §5.2's per-(chemical x parameter) granularity.
    //
    // 0.35f is a deliberately cautious "low but functional" starting
    // trust, not fleet-validated: high enough that a correctly-specified
    // chemical can actually move the allocator's solution from day one
    // (matches §8.5's "no dosing before baseline test" intent -- once
    // that test lands, dosing should actually happen), low enough that
    // real observed response (once the allocator/learning loop actually
    // refines confidence from outcomes) still has meaningful room to
    // raise or correct it rather than starting already-saturated at 1.0.
    // Revisit once real per-chemical response data exists to justify a
    // different starting number (§10-style open item, not asserted as
    // final here).
    const float kColdStartConfidence = 0.35f;
    for (int p = 0; p < kNumParams; p++) {
        if (chemicals[numChemicals].potencyPerMl[p] != 0.0f) {
            chemicals[numChemicals].confidence[p] = kColdStartConfidence;
        }
    }

    numChemicals++;
}

bool AIEngineV2::removeChemical(int index) {
    if (index < 0 || index >= numChemicals) return false;

    // §5.2 sufficiency check before committing the removal.
    ChemicalDeclaration trial[kMaxChemicals];
    int trialCount = 0;
    for (int i = 0; i < numChemicals; i++) {
        if (i == index) continue;
        trial[trialCount++] = chemicals[i];
    }

    // Fixed 2026-07-28: was unconditionally using the raw target VALUES
    // (targetAlkDkh.value etc.) as "desired," regardless of whether any
    // measurement had ever been taken -- meaning this check believed a
    // full correction was always needed even on a device that had never
    // ingested a single reading, incorrectly blocking removal of the only
    // chemical that could address Alk/Ca even with nothing actually wrong
    // yet. Mirrors recalculate()'s real logic instead: skip any parameter
    // whose filter isn't initialized (§8.5, matches "no dosing before
    // baseline test" exactly), and for anything that IS initialized, use
    // the actual gap between the current estimated level and target, not
    // the target number alone. Deliberately NOT step-capped by maturity
    // the way a daily dosing request is -- sufficiency is asking "could
    // this genuinely be corrected if needed," not "what's today's
    // throttled request."
    float desired[kNumParams] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (filters[P_ALK].initialized) desired[P_ALK] = fmaxf(0.0f, targetAlkDkh.value - filters[P_ALK].level);
    if (filters[P_CA].initialized)  desired[P_CA]  = fmaxf(0.0f, targetCaPpm.value  - filters[P_CA].level);
    if (filters[P_MG].initialized)  desired[P_MG]  = fmaxf(0.0f, targetMgPpm.value  - filters[P_MG].level);

    DosingPlanV2 dummy;
    bool sufficient = Allocator::solve(trial, trialCount, desired, safety, true, 8.2f, dummy);
    if (!sufficient) return false; // caller must surface this as a customer warning (§5.2)

    for (int i = 0; i < trialCount; i++) chemicals[i] = trial[i];
    numChemicals = trialCount;
    return true;
}

// =============================================================================
// §3.5 Environmental Events Logging -- see the type/method comments in
// AI_EngineV2.h for the full reasoning. Implementations below.
// =============================================================================

void AIEngineV2::logWaterChange(float fractionChanged, const SaltMixProfile& mix) {
    fractionChanged = constrain(fractionChanged, 0.0f, 1.0f);
    if (fractionChanged <= 0.0f) return;

    // §3.5.1 safety fallback: an unknown/custom salt mix with no confident
    // target values must not guess -- exclude this window from the
    // dilution correction entirely rather than risk applying a wrong
    // number and corrupting the filter's level with a bad discontinuity.
    // The water change still physically happened and will still show up
    // in the next real measurement; without a known target this class
    // just can't explain it, same honest limitation as any other
    // unmodeled external event.
    if (!mix.known) return;

    // expected_post_change = old_value * (1 - fraction) + salt_mix_value * fraction
    // Applied directly to each initialized filter's level -- same "feed a
    // known deterministic effect into the estimate" pattern advanceTime()
    // already uses for known dose effects (see that function's own
    // comment), so the NEXT real measurement's innovation reflects only
    // genuinely unexplained change, not this dilution. Deliberately does
    // NOT touch trend/covariance/confidence -- see the method's header
    // comment for why.
    if (filters[P_ALK].initialized) {
        filters[P_ALK].level = filters[P_ALK].level * (1.0f - fractionChanged) + mix.alkDkhAtFull * fractionChanged;
    }
    if (filters[P_PH].initialized && mix.phAtFull > 0.0f) {
        filters[P_PH].level = filters[P_PH].level * (1.0f - fractionChanged) + mix.phAtFull * fractionChanged;
    }
    if (filters[P_CA].initialized) {
        filters[P_CA].level = filters[P_CA].level * (1.0f - fractionChanged) + mix.caPpmAtFull * fractionChanged;
    }
    if (filters[P_MG].initialized) {
        filters[P_MG].level = filters[P_MG].level * (1.0f - fractionChanged) + mix.mgPpmAtFull * fractionChanged;
    }
}

void AIEngineV2::logLivestockAddition(BioloadMagnitude magnitude) {
    // Starts (or restarts, if already inside a window) the tolerance-
    // widening clock used by ingestMeasurement()'s confidence-learning
    // guard. Window length scales with magnitude -- a larger addition
    // plausibly takes longer to settle into its new steady-state demand
    // than a small one, though neither number is fleet-validated (§10-style
    // open item, same caveat as elsewhere in this file). The actual window
    // length used is read from ingestMeasurement() (currently a fixed 21
    // days there) -- magnitude is accepted and stored for future use
    // (e.g. scaling that window per-magnitude) but not yet wired to vary
    // it, kept simple for this first version.
    (void)magnitude; // reserved for a future per-magnitude window; not yet used
    daysSinceLivestockAddition = 0.0f;
}

void AIEngineV2::ageEnvironmentalEvents(float dtDays) {
    if (daysSinceLivestockAddition >= 0.0f) {
        daysSinceLivestockAddition += dtDays;
    }
}

void AIEngineV2::recordManualDose(int chemicalIndex, float mlDosed) {
    if (chemicalIndex < 0 || chemicalIndex >= numChemicals) return; // pump not mapped to a declared chemical -- see header comment
    if (!isfinite(mlDosed) || mlDosed <= 0.0f) return;

    const ChemicalDeclaration& chem = chemicals[chemicalIndex];
    for (int p = 0; p < kNumParams; p++) {
        if (!filters[p].initialized) continue; // nothing to correct on an uninitialized filter -- matches recalculate()'s own gate
        float effect = chem.potencyPerMl[p] * chem.confidence[p] * mlDosed;
        if (effect == 0.0f) continue;

        // Applied once, immediately -- this mL amount has already been
        // fully delivered by the time this is called (unlike advanceTime's
        // per-day rate, there's no dtDays to scale by here).
        filters[p].level += effect;
        pendingContribution[chemicalIndex][p] += effect;
    }
}


// =============================================================================
// §4.5 Persistence — NVS (Preferences), not raw LittleFS. Chosen because:
// (a) the state is small (~250 bytes: 4 Kalman filters + up to 8 chemicals'
//     confidence), comfortably inside NVS blob limits;
// (b) NVS writes are transactional/power-loss-safe by construction, so we
//     don't have to hand-roll atomic write-then-rename the way a raw
//     LittleFS file would need;
// (c) this codebase already uses Preferences elsewhere (calibration/config)
//     — reusing the pattern rather than introducing a second persistence
//     mechanism for this one feature.
// =============================================================================
namespace {
constexpr char kNvsNamespace[] = "aiengine2";
constexpr char kNvsStateKey[]  = "state_v1";
constexpr char kNvsCrcKey[]    = "state_v1_crc";

// Small, dependency-free CRC32 (standard IEEE 802.3 polynomial). Only used
// for corruption detection on a few-hundred-byte blob, not a security
// boundary — no need to pull in a hardware CRC dependency for this.
uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}
} // namespace

bool AIEngineV2::saveState() {
    PersistedStateV1 state; // magic/schemaVersion default-initialized correctly

    for (int p = 0; p < kNumParams; p++) state.filters[p] = filters[p];

    // Added 2026-08-04: see PersistedStateV1's schemaVersion comment for
    // why this needs to survive a reboot -- a rolling 7/30-day window
    // that resets every reboot could realistically never accumulate
    // enough data to report anything on a device that reboots as often
    // as this fleet's test devices have tonight.
    for (int p = 0; p < kNumParams; p++) {
        for (int i = 0; i < kHistoryCapacity; i++) state.history[p][i] = history[p][i];
        state.historyNextSlot[p] = historyNextSlot[p];
    }
    state.totalElapsedDays = totalElapsedDays;

    // Confidence keyed by name (§5.2) — see header comment. Only the
    // currently-declared chemicals are written; a removed chemical's old
    // confidence is simply not carried forward.
    state.numConfidenceEntries = 0;
    for (int i = 0; i < numChemicals && state.numConfidenceEntries < kMaxChemicals; i++) {
        PersistedChemicalConfidence& entry = state.confidenceByName[state.numConfidenceEntries];
        strncpy(entry.name, chemicals[i].name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        for (int p = 0; p < kNumParams; p++) entry.confidence[p] = chemicals[i].confidence[p];
        state.numConfidenceEntries++;
    }

    uint32_t checksum = crc32(reinterpret_cast<const uint8_t*>(&state), sizeof(state));

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return false;

    size_t written = prefs.putBytes(kNvsStateKey, &state, sizeof(state));
    bool ok = (written == sizeof(state));
    if (ok) {
        ok = (prefs.putUInt(kNvsCrcKey, checksum) == sizeof(uint32_t));
    }
    prefs.end();
    return ok;
}

bool AIEngineV2::restoreState() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return false;

    PersistedStateV1 state;
    size_t readLen = prefs.getBytes(kNvsStateKey, &state, sizeof(state));
    uint32_t storedCrc = prefs.getUInt(kNvsCrcKey, 0);
    prefs.end();

    // No entry yet (first boot) or a short/mismatched read.
    if (readLen != sizeof(state)) return false;
    // Wrong/corrupt blob, or a schema this build doesn't know how to read —
    // §8's safe-defaults fallback handles both identically. A future schema
    // bump (kSchemaVersion) is a deliberate migration point, not a silent
    // best-effort read of a differently-shaped struct.
    if (state.magic != PersistedStateV1::kMagic) return false;
    if (state.schemaVersion != PersistedStateV1::kSchemaVersion) return false;
    // Detects a write torn by power loss/reboot mid-save — Preferences'
    // own transactional guarantee should prevent this in practice, but the
    // checksum costs almost nothing and removes the "should" from that.
    uint32_t computedCrc = crc32(reinterpret_cast<const uint8_t*>(&state), sizeof(state));
    if (computedCrc != storedCrc) return false;

    for (int p = 0; p < kNumParams; p++) filters[p] = state.filters[p];

    // Added 2026-08-04: see PersistedStateV1's schemaVersion comment for
    // the full reasoning -- this is what actually lets the Week/Month
    // buffer survive a reboot instead of resetting empty every time.
    for (int p = 0; p < kNumParams; p++) {
        for (int i = 0; i < kHistoryCapacity; i++) history[p][i] = state.history[p][i];
        historyNextSlot[p] = state.historyNextSlot[p];
    }
    totalElapsedDays = state.totalElapsedDays;

    // Fixed 2026-07-31: corrects the pTrend seeding bug (see update()'s
    // comment for the full explanation) for state that was already
    // persisted BEFORE this fix existed. The bad seed only happens once,
    // at first-ever init, so simply shipping the corrected seed has no
    // effect on a filter that's already initialized -- this catches that
    // case on restore instead, without a full reset. Deliberately
    // conservative: only clamps pTrend DOWN if it's still sitting near the
    // old (too-large) seed magnitude, leaving level/pLevel/bestPLevel (and
    // therefore proven maturity, and the real level estimate itself)
    // completely untouched. A filter that's already had pTrend properly
    // corrected by real data (pCross has built up, kTrend has actually
    // shrunk it) is left alone -- this only catches the specific stuck-at-
    // initial-seed case, not a general "trend uncertainty is high" state
    // that might be genuinely earned from noisy real data.
    for (int p = 0; p < kNumParams; p++) {
        if (!filters[p].initialized || filters[p].initialPLevel <= 0.0f) continue;
        float oldBuggyCeiling = filters[p].initialPLevel * 0.99f; // was seeded at 1.0x; small margin for float drift
        if (filters[p].pTrend >= oldBuggyCeiling) {
            filters[p].pTrend = filters[p].initialPLevel * 0.1f; // matches the corrected seed formula
        }
    }

    // Match by name, not index (§5.2) — an inventory edit between reboots
    // must only affect the changed chemical's confidence, not misassign
    // everyone else's by shifted position. A currently-declared chemical
    // with no match in the saved state is new since the last save and
    // keeps its wizard-seeded stoichiometry confidence untouched.
    for (int i = 0; i < numChemicals; i++) {
        for (int j = 0; j < state.numConfidenceEntries; j++) {
            if (strncmp(chemicals[i].name, state.confidenceByName[j].name,
                        sizeof(chemicals[i].name)) == 0) {
                for (int p = 0; p < kNumParams; p++) {
                    chemicals[i].confidence[p] = state.confidenceByName[j].confidence[p];
                }
                break;
            }
        }
    }
    return true;
}
