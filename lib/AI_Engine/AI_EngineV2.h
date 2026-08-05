#pragma once
#include <Arduino.h>
#include <math.h>

// =============================================================================
// AI_Engine v2 — Core types
//
// Maps to spec sections:
//   §5   Mode Consolidation -> Chemical Inventory Declaration
//   §5.1 Allocator as constrained optimization (NNLS)
//   §5.2 Per-(chemical x parameter) confidence
//   §4.5 Kalman filter (level + trend, one per water parameter)
//   §6/§7 Shared tank-scaled estimator for baselines/caps/targets
//   §7   "recommend, never silently impose" for every produced number
//
// Still-open items from spec §10 that this file deliberately does NOT
// hardcode a guess for (see MIGRATION_NOTES.md):
//   - exact safety-cap scaling multiplier per category
//   - named commercial preset table
//
// §4.5 Kalman/confidence persistence across reboots: IMPLEMENTED via NVS
// (Preferences), see PersistedStateV1 below and saveState()/restoreState()
// in AI_EngineV2.cpp.
// =============================================================================

static constexpr int kMaxChemicals = 8;   // ETL fixed_vector is the production
static constexpr int kNumParams    = 4;   // recommendation (§5.1) instead of

// Which water parameter. Order is fixed and used as the matrix index
// everywhere below — do not reorder without updating persisted state.
enum WaterParam : uint8_t { P_ALK = 0, P_PH = 1, P_CA = 2, P_MG = 3 };

// -----------------------------------------------------------------------
// §3.5 Environmental Events Logging — added 2026-08-04.
//
// Why this exists, in the spec's own words: "without this, the system has
// no way to distinguish 'the water changed because of dosing' from 'the
// water changed because of something external.' That corruption would be
// silent and very hard to notice later, since everything would still look
// like normal learning — it would just be learning the wrong thing."
//
// Directly closes a gap the confidence-learning feedback loop
// (pendingContribution/updateConfidenceFromOutcome, see below) had to work
// around with only a crude sanity-check guard (skip if the predicted/
// actual ratio looks implausible) since it had no way to know WHY an
// implausible ratio happened. This section gives it the real answer
// instead of a guess.
// -----------------------------------------------------------------------

// §3.5.1 Named Salt Mix Profile preset — same pattern as chemical presets
// (ChemicalPresets.h): a customer picks a known product instead of typing
// exact chemistry. Values are what a FRESH batch of this salt mix reads at
// full (undiluted) strength, used by the dilution formula below.
struct SaltMixProfile {
    char name[24] = {0};
    float alkDkhAtFull = 0.0f;
    float caPpmAtFull = 0.0f;
    float mgPpmAtFull = 0.0f;
    float phAtFull = 0.0f;
    // §3.5.1 safety fallback: "if the salt mix is unknown/custom with no
    // confident target values, do not guess — exclude that reconciliation
    // window from learning entirely rather than risk corrupting the
    // signal with a wrong dilution estimate." false = exactly that case.
    bool known = true;
};

// §3.5.2 Livestock/bioload addition — "simpler than water changes, no
// precise math needed, this is context for the Month layer's drift
// detection, not a dilution correction."
enum class BioloadMagnitude : uint8_t { Small = 0, Medium = 1, Large = 2 };

// -----------------------------------------------------------------------
// §1 / Core Design Principle: every produced number is a *recommendation*.
// Wrapping every customer-facing number in the same small struct means the
// "never silently overwrite a customization" rule is enforced in one place
// instead of re-implemented per feature.
// -----------------------------------------------------------------------
struct Recommendable {
    float value = 0.0f;
    bool  customerOverridden = false;   // true once the customer edits it
    float systemSuggestion   = 0.0f;    // what the estimator currently thinks

    // Called whenever the estimator recomputes (tank size changed, coral
    // load changed, etc). Never touches `value` if the customer already
    // customized it — that is the one rule this whole struct exists to
    // enforce. (§ Core Design Principle)
    void updateSuggestion(float newSuggestion) {
        systemSuggestion = newSuggestion;
        if (!customerOverridden) value = newSuggestion;
    }

    void applyOverride(float newValue) {
        value = newValue;
        customerOverridden = true;
    }
};

// -----------------------------------------------------------------------
// §6 Tank profile — the single input that drives baselines, safety caps,
// and target ranges via one shared estimator (Core Design Principle: "the
// same estimator mechanism across all of them").
// -----------------------------------------------------------------------
enum class CoralLoad : uint8_t { Light = 0, Moderate = 1, Heavy = 2, SPSDominant = 3 };

struct TankProfile {
    float tankVolumeLiters = 0.0f;
    CoralLoad coralLoad = CoralLoad::Moderate;

    // §6/§7: reference tank used for the v1 scaling formula, kept so the
    // new estimator produces numbers continuous with existing tuned
    // systems (reefDoser1-7) rather than a discontinuous jump on migration.
    static constexpr float kReferenceVolumeLiters = 1135.6f; // 300G
};

// -----------------------------------------------------------------------
// §5 Chemical inventory declaration — replaces the v1 `mode` int (1-8).
// The customer declares what they have; the allocator decides how to use
// it (§5: "the engine decides *how* to use it, not the customer").
// -----------------------------------------------------------------------
struct ChemicalDeclaration {
    char name[24] = {0};

    // Potency: how much this chemical moves each parameter per mL.
    // Signed — most entries are 0 except the 1-2 parameters a chemical
    // actually affects, but the allocator does NOT restrict a chemical to
    // only its "traditional" role (§5.1: "fully flexible").
    float potencyPerMl[kNumParams] = {0, 0, 0, 0};

    // §7: hard per-chemical volumetric cap. Never adjusted by the learning
    // system — only the strategy within it moves.
    float maxMlPerDay = 0.0f;

    // §5.1/§5.2: confidence per (chemical x parameter) cell, 0..1.
    // Seeded from stoichiometry (potencyPerMl) at declaration time, refined
    // by the Kalman-informed allocator as real data comes in. A confidence
    // of 0 for a given [param] means "this chemical is not currently
    // trusted to move that parameter," independent of the other cells.
    float confidence[kNumParams] = {0, 0, 0, 0};

    // §5.1: continuous pH-impact weight, replacing v1's hard naohMaxPh
    // cutoff. 1.0 = no pH-based derating. A chemical with no material
    // pH side-effect (e.g. CaCl2) should stay at 1.0 always.
    // Populated from a simple curve in Allocator::phDerateWeight(), not a
    // step function (§5.1 "Refinement — continuous pH-impact weighting").
    bool phSensitive = false;

    // Added 2026-07-28, generalizing V1's Mode-7-only "day/night Alk split"
    // (hardcoded to exactly P3 NaOH vs P4 Alk) into something any
    // phSensitive chemical on any tank can use. 0-100: how strongly to
    // suppress this chemical's allocation weight during lights-on hours,
    // independent of pH-ceiling proximity. Default 0 = no change beyond
    // the existing phSensitive derate/night-boost. At 100, daytime weight
    // goes effectively to zero -- the allocator will use other declared
    // chemicals for the same parameter instead during the day (e.g. a
    // non-pH-raising Alk source), same practical outcome as V1's hard 0%/
    // 100% split, but expressed as a weight so the allocator can still fall
    // back to this chemical if genuinely nothing else can meet the
    // correction (matches how phSensitive/confidence already degrade
    // gracefully elsewhere, rather than a hard gate that can misfire into
    // a false "insufficient" the way an earlier attempt at this did).
    // Only meaningful when phSensitive is true; see
    // Allocator::solve()'s phWeight computation for where this applies.
    float daytimeSuppressPercent = 0.0f;

    bool active = true;   // false = declared-but-currently-disabled (§5.2 removal)
};

// -----------------------------------------------------------------------
// §4.5 Kalman filter, one per water parameter. Stage 1 (per spec's own
// recommended staged build): scalar constant-velocity filter — state =
// [level, trend] — plain 2x2 math, no matrix library dependency yet.
// Stage 2 (production): swap in ESP-DSP's EKF for full cross-effect
// coupling once this is validated against the synthetic simulator (§9.5).
// -----------------------------------------------------------------------
struct ParamKalmanState {
    float level = 0.0f;          // current best estimate of the parameter
    float trend = 0.0f;          // estimated rate of change, per day
    float pLevel = 1.0f;         // covariance — level uncertainty
    float pTrend = 1.0f;         // covariance — trend uncertainty
    float pCross = 0.0f;         // level/trend cross-covariance
    bool  initialized = false;
    // Set once, on the first real update, to that update's measurementNoise
    // (see MathEngine::update). Fixed 2026-07-24: maturity() previously
    // normalized pLevel against a fixed constant (1.0), which is
    // unit-scale-dependent -- it happened to work for ppm-scale Ca/Mg
    // (noise ~5-30) but badly broke for dKH/pH-scale Alk/pH (noise
    // ~0.02-0.15), which read as 87-98% "mature" after a SINGLE real
    // measurement, permanently capping their correction speed at the
    // low "well-established tank" ceiling from day one -- found via the
    // §9.5 synthetic digital-twin simulator, which showed Alk crashing
    // toward zero because the allowed correction rate (already at
    // matureMax due to this bug) couldn't keep up with even modest
    // simulated consumption. Normalizing against the filter's OWN
    // starting uncertainty instead makes maturity scale-invariant: it
    // always starts at 0 on the first real measurement (pLevel ==
    // initialPLevel) and rises only as pLevel shrinks meaningfully
    // relative to where THIS parameter started, regardless of units.
    float initialPLevel = 0.0f;

    // Added 2026-07-27: pLevel legitimately GROWS between measurements
    // (see MathEngine::predict's process-noise term) -- that's correct for
    // maturity()'s original purpose (§4.4, throttling correction
    // aggressiveness: a stale estimate SHOULD make the system more
    // cautious right now). But that same decaying number was also being
    // used to drive the customer-facing "test less often over time"
    // feature, where a confidence score that quietly regresses hours after
    // a good test — with no new bad data, just time passing — doesn't
    // match what "the system has proven itself" should mean to a
    // customer, and undermines trust in the recommendation. This tracks
    // the best (lowest) pLevel ever achieved, updated only inside
    // MathEngine::update()'s shrink step, never by predict()'s growth --
    // a genuine ratchet, unaffected by how much time has passed since the
    // last measurement.
    float bestPLevel = -1.0f; // -1 = "never updated yet" sentinel

    // §4.4 Maturity Arc: falls directly out of covariance, no separate
    // "confidence score" field needed — this *is* it. Real-time: reflects
    // how much to trust the CURRENT estimate right now, including staleness
    // since the last measurement. Drives correction-aggressiveness
    // throttling (maxCorrectionStep) -- must be allowed to decay when data
    // is stale, that's the whole point of it existing.
    float maturity() const {
        if (!initialized || initialPLevel <= 0.0f) return 0.0f;
        float ratio = pLevel / initialPLevel; // 1.0 (brand new) shrinking toward 0 (mature)
        return constrain(1.0f - ratio, 0.0f, 1.0f);
    }

    // Cumulative, ratcheting version: "how well has this filter EVER been
    // proven to predict this tank," independent of how long it's been
    // since the last test. Only ever equal to or better than what it was
    // last time this is checked -- never quietly regresses just because
    // time passed. This is what should drive the manual-test-interval
    // recommendation, not maturity() above.
    float provenMaturity() const {
        if (!initialized || initialPLevel <= 0.0f || bestPLevel < 0.0f) return 0.0f;
        float ratio = bestPLevel / initialPLevel;
        return constrain(1.0f - ratio, 0.0f, 1.0f);
    }
};

// Per-source measurement noise (§3.2 / §4.5 table row "API-vs-manual trust
// weighting"). Trident/Apex are trusted more than a manual titration.
enum class MeasurementSource : uint8_t { ManualTest = 0, ApexTrident = 1, HannaChecker = 2 };

// -----------------------------------------------------------------------
// Replaces v1's DosingPlan (which only had fixed named fields per legacy
// chemical). v2's plan is indexed by declared-chemical slot, since the
// chemical list is now customer-defined, not a fixed set of 6 legacy names.
// -----------------------------------------------------------------------
struct DosingPlanV2 {
    float mlPerDay[kMaxChemicals] = {0};
    uint8_t numChemicals = 0;

    // Explainability requirement (§1: "every dosing decision must be
    // explainable in plain English, and fully auditable").
    char explanation[256] = {0};
};

// -----------------------------------------------------------------------
// §7 Safety envelope. Defaults are tank/load-scaled recommendations
// (Recommendable), never silently imposed, never loosened by learning.
// -----------------------------------------------------------------------
struct SafetyEnvelope {
    Recommendable maxAlkRisePerDayDkh;   // §4.4/§7 capped-recovery-rate
    Recommendable maxPhRisePerDay;
    Recommendable maxCaRisePerDayPpm;
    Recommendable maxMgRisePerDayPpm;
    // §5.1 pH-sensitive-chemical ceiling (v1: mode7NaohMaxPh, default 8.45).
    // Resolved 2026-07-24 -- was hardcoded in Allocator.cpp with a comment
    // marking it "until wired to the per-tank Recommendable"; now it is.
    // A value of 0.0 (Recommendable's own zero-initialized default) means
    // "not yet configured by the caller" -- Allocator::solve() falls back
    // to a safe default (8.60, matching §1's pH target range) rather than
    // silently gating every pH-sensitive chemical to zero, which is what
    // treating an unconfigured 0.0 as a literal ceiling would do.
    Recommendable naohPhCeiling;
    // Per-chemical caps live on ChemicalDeclaration.maxMlPerDay directly,
    // also produced via Recommendable at declaration time in the wizard
    // layer (not duplicated here).
};

// -----------------------------------------------------------------------
// §6/§7 Shared estimator. Core Design Principle: "using the same estimator
// mechanism across all of them" — one function, reused for initial
// baselines, safety-cap defaults, and target-range defaults alike, instead
// of three separately-tuned code paths. The exact per-category multiplier
// table is a §10 open item (not guessed at here — see MIGRATION_NOTES.md);
// this implements the *shape* of the estimator (linear in tank volume,
// stepped by coral load) so the multiplier table can be dropped in once
// confirmed.
// -----------------------------------------------------------------------
namespace TankEstimator {
    // Baseline demand estimates (§6) are corrected over time by the Kalman
    // learner (§4.5, ParamKalmanState::maturity) as real per-tank data comes
    // in — the Observation/Break-in Period (§8) exists precisely so a rough
    // starting guess here is fine. Safety caps (§7) are the opposite: the
    // spec is explicit that the learning system never adjusts the envelope,
    // no matter how confident it gets — so a cap's starting value has no
    // later correction mechanism except a customer manually changing it.
    // Two purposes, two multiplier tables, same shape.
    enum class Purpose : uint8_t { BaselineDemand = 0, SafetyCap = 1 };

    // referenceValue is the known-good tuned number for the 300G reference
    // tank (kReferenceVolumeLiters) at Moderate load — i.e. today's tuned
    // production constants (limits.maxKalkDay etc.) become the reference
    // inputs during migration, so existing customers see continuity.
    float scale(const TankProfile& tank, float referenceValueAtModerateLoad,
                Purpose purpose = Purpose::BaselineDemand);
}

// -----------------------------------------------------------------------
// §4.5 Math engine: predict/update for one parameter's Kalman filter.
// Architecturally separate from dose execution (§7 "math engine vs.
// execution engine") — this file produces mL/day *recommendations* only;
// a separate FreeRTOS pump task (unchanged from v1's Doser lib) is
// responsible for metering them out precisely.
// -----------------------------------------------------------------------
namespace MathEngine {
    // Advances the filter by `dtDays` with no new measurement (predict step,
    // §3.1 extrapolation between tests). Process noise q tunes how quickly
    // the filter trusts drift vs. holds its prior estimate.
    void predict(ParamKalmanState& s, float dtDays, float processNoise);

    // measurementNoise varies by BOTH source (§3.2) and parameter — each
    // water parameter has its own scale (dKH/pH-units/ppm/ppm), so noise
    // must be expressed in that parameter's own units, not one flat number
    // applied everywhere. Pass a larger value for less-trusted sources so
    // the filter weights it less.
    void update(ParamKalmanState& s, float measurement, float measurementNoise);

    float measurementNoiseFor(WaterParam param, MeasurementSource source);

    // §4.4: max single-step correction size, gated by this filter's own
    // maturity (covariance) — replaces v1's hand-tuned two-speed
    // fastAlk boost/step-size ladder with one falls-out-for-free rule.
    float maxCorrectionStep(const ParamKalmanState& s, float youngTankMax, float matureTankMax);

    // §3.5.3 pH diurnal swing — a natural, recurring, non-dosing effect
    // (photosynthesis raises pH during lights-on hours, respiration lowers
    // it at night). Returns the EXPECTED pH change over dtDays from this
    // cycle alone, light-state-aware (same light input already required
    // for allocator routing, §5). Subtracted out before pH's confidence-
    // learning step attributes any remaining change to dosing (see
    // updateConfidenceFromOutcome's pH handling in AI_EngineV2.cpp) — NOT
    // applied to the filter's level directly, since unlike a water change
    // this swing is naturally self-reversing within 24h and the filter's
    // own trend term should track real net pH drift, not have a cyclical
    // swing baked into it as a permanent shift.
    //
    // Deliberately a simple two-state (lights on/off) rate model for this
    // first version, not a smooth continuous curve through the day — this
    // class only has a light-state boolean available, not elapsed time
    // within the current light/dark period. The assumed daily swing
    // magnitude (~0.15 pH) is a reasoned starting point consistent with
    // commonly-reported reef-tank diurnal swings, not fleet-validated —
    // same caveat as every other unvalidated constant in this file.
    float expectedDiurnalPhDelta(bool lightsActive, float dtDays);
}

// -----------------------------------------------------------------------
// Top-level orchestrator — the v2 replacement for AIEngine::calculateNextPlan.
// Note what is deliberately gone: the `mode` int parameter. There is no
// mode switch anymore (§5).
// -----------------------------------------------------------------------
class AIEngineV2 {
public:
    // Added 2026-08-04, §4.2/§4.3: public so the free helper functions in
    // AI_EngineV2.cpp that compute Week/Month trends can read it -- the
    // actual storage (history[][], further below) stays private; this is
    // just the plain data-holding type itself, same level of exposure as
    // ParamKalmanState and every other struct already public in this file.
    struct HistoryEntry { float value = 0.0f; float recordedAtDay = -1.0f; bool valid = false; };

    TankProfile tank;
    SafetyEnvelope safety;
    ChemicalDeclaration chemicals[kMaxChemicals];
    int numChemicals = 0;

    ParamKalmanState filters[kNumParams];   // one per water parameter (§4.5)

    // Fixed 2026-07-24, found via the §9.5 synthetic digital-twin
    // simulator: tracks the last computed plan's per-chemical mL/day so
    // advanceTime() can feed the KNOWN effect of already-standing dosing
    // into the predict step (see advanceTime()'s own comment for why this
    // matters -- in short, without it `trend` converges to a permanently
    // wrong, partially-cancelled steady state instead of the true
    // consumption rate). Not part of the persisted state (§4.5 scope stays
    // narrow -- learned Kalman/confidence state only); losing this across
    // a reboot just means one predict cycle right after boot doesn't
    // account for pre-reboot dosing, which self-corrects within a cycle.
    float lastMlPerDay[kMaxChemicals] = {0};

    // Added 2026-08-04: implements the feedback arrow explicitly drawn in
    // the spec's own §2 architecture diagram -- "(next input) -> compare
    // predicted vs. actual response -> feeds back into learning system" --
    // which never actually existed until now. Confidence was seeded once
    // at addChemical() (stoichiometric cold-start, 0.35) and never touched
    // again regardless of how well or poorly a chemical's real-world
    // effect matched what its confidence predicted. This is the missing
    // half of §4.1's Day-layer reconciliation: MathEngine::update()
    // already computes `innovation` (predicted vs. actual) on every real
    // measurement, but discarded it instead of attributing it back to the
    // specific chemicals responsible for that parameter's predicted
    // change. See updateConfidenceFromOutcome() in AI_EngineV2.cpp.
    //
    // Tracks, per (chemical, parameter), the cumulative known dose effect
    // that chemical has injected into that parameter's level since the
    // last real measurement -- i.e. each chemical's individual SHARE of
    // advanceTime()'s existing `doseEffect` sum, not a new quantity, just
    // no longer thrown away after being summed. Session-only, like
    // lastMlPerDay above -- not persisted (see that field's own comment
    // for why: losing one window's attribution on reboot just means one
    // measurement's worth of learning doesn't happen, self-corrects next
    // cycle, not worth the persistence complexity for this).
    float pendingContribution[kMaxChemicals][kNumParams] = {{0}};

    // Added 2026-08-04, §3.5.2: days since the last logged livestock/
    // bioload addition, one per parameter since different additions
    // (coral vs. fish, per the spec's own note field) may plausibly affect
    // different parameters differently -- kept simple as a single shared
    // clock for now since the spec doesn't ask for per-parameter event
    // logging, just widens tolerance uniformly for a window after ANY
    // logged addition. -1 = "no addition logged yet / long enough ago not
    // to matter." Session-only, like pendingContribution -- see that
    // field's comment for why this class's persistence stays narrow.
    float daysSinceLivestockAddition = -1.0f;

    // Added 2026-08-04, needed by the §3.5.3 pH diurnal correction and by
    // ingestMeasurement()'s confidence-learning step: recalculate() already
    // receives fresh lightsActive/dtDays each cycle, but ingestMeasurement()
    // doesn't take either as a parameter (kept its existing signature
    // stable rather than touching every call site in main.cpp) -- caching
    // them here instead. Session-only, not persisted, same reasoning as
    // pendingContribution above.
    bool lastKnownLightsActive = false;
    float daysSinceLastMeasurement[kNumParams] = {0, 0, 0, 0};

    // -----------------------------------------------------------------------
    // §4.2/§4.3 Week and Month layers -- added 2026-08-04.
    //
    // §4.5's own table frames Day-layer reconciliation and extrapolation as
    // the existing Kalman filter's update/predict steps -- already built,
    // not duplicated here. What's genuinely separate and still missing:
    //
    // §4.2 Week layer: "a rolling 7-day real average" -- the spec is
    // explicit this should be its own recalculation of "what's this tank's
    // actual baseline demand," distinct from the filter's single blended
    // trend estimate. A short window and a long window measuring the same
    // underlying trend can genuinely disagree in ways one blended number
    // can't reveal on its own.
    //
    // §4.3 Month layer: "detect slow drift... shift the baseline ahead of
    // the day/week layers having to chase a moving target." Implemented as
    // comparing the week-window trend against a longer (30-day) window --
    // real, sustained divergence between them IS drift, in a way a single
    // Kalman trend value can't distinguish from ordinary noise on its own.
    //
    // §4.4 anomaly flagging: "a miss bigger than the mature-phase
    // correction cap would allow gets flagged as an anomaly to the
    // customer... rather than silently folded into the next dose."
    // updateConfidenceFromOutcome's existing ratio guard already detects
    // exactly this case -- it just silently skipped learning before now,
    // with nothing surfaced to anyone. lastAnomaly[] below is what makes
    // that visible instead of silent.
    //
    // Fixed-capacity circular buffer per parameter (ETL-style bounded
    // memory, matching §5.1's own recommendation for exactly this kind of
    // history, though implemented here as a plain fixed array consistent
    // with this file's existing style rather than pulling in the ETL
    // library for one bounded buffer). kHistoryCapacity=12 is enough for
    // roughly a month of real testing at a realistic every-2-3-day cadence
    // without being large -- deliberately NOT one entry per day, since
    // real testing is never that regular in practice.
    static constexpr int kHistoryCapacity = 12;
    HistoryEntry history[kNumParams][kHistoryCapacity];
    int historyNextSlot[kNumParams] = {0, 0, 0, 0};
    float totalElapsedDays = 0.0f; // monotonic clock for history timestamps, incremented by advanceTime()

    // Set by updateConfidenceFromOutcome() when a real measurement's
    // outcome falls outside the plausible ratio bounds AND no recent
    // logged environmental event (§3.5.2) explains it -- see that
    // function's own comment in the .cpp for the full reasoning. The
    // caller (main.cpp) is expected to check this after every
    // ingestMeasurement() call and surface it (log, dashboard, wherever
    // "flag it to the customer" should actually happen) -- this class
    // stays pure math/state, no direct Serial/logger calls from in here.
    bool lastAnomaly[kNumParams] = {false, false, false, false};

    // §3.5.1 Water change reconciliation. Call as soon as a water change is
    // logged (dashboard event, §3.5.1) -- applies the dilution formula
    // directly to each initialized filter's level, the same "feed a known
    // deterministic effect straight into the estimate" pattern advanceTime()
    // already uses for known dose effects, so the NEXT real measurement's
    // innovation reflects only genuinely unexplained change, not dilution.
    // fractionChanged: 0.0-1.0 (e.g. 0.15 for a 15% water change).
    // Deliberately does NOT touch confidence or pendingContribution -- a
    // water change dilutes the WATER, not the chemicals' known potency;
    // those remain valid and untouched.
    void logWaterChange(float fractionChanged, const SaltMixProfile& mix);

    // §3.5.2 Livestock/bioload addition. No math applied directly (per
    // spec: "no precise math needed") -- just starts the clock the
    // confidence-learning guard (see updateConfidenceFromOutcome in
    // AI_EngineV2.cpp) and future Month-layer drift detection can check
    // against, so a legitimate post-addition demand shift isn't
    // misattributed as chemical-effectiveness noise the way an
    // unexplained shift of the same size would be.
    void logLivestockAddition(BioloadMagnitude magnitude);

    // Call once per day (or whatever cadence advanceTime() itself runs at)
    // to age out the livestock-addition window. Cheap enough to just call
    // unconditionally from wherever advanceTime() is already called.
    void ageEnvironmentalEvents(float dtDays);

    // Added 2026-08-04, at owner's direct observation: a manual Quick Dose
    // physically puts real chemical into the tank exactly like an
    // AI-planned dose does, but until now was completely invisible to this
    // class -- advanceTime()'s known-dose-effect calculation only ever
    // knew about lastMlPerDay (the AI's OWN plan), so a manual dose's real
    // effect on the water would show up in the next measurement with no
    // explanation, at real risk of getting misattributed to whichever
    // chemical the AI itself happened to be dosing that cycle -- the exact
    // same "unlogged real-world event corrupts the learning signal" risk
    // §3.5 exists to prevent, just from a different real-world source.
    //
    // Call immediately after a manual dose physically completes (see
    // handlePostLiveDose() in WebRoutes.cpp) with the SAME chemical index
    // used everywhere else in this class (chemicals[i]), not a raw pump
    // number -- the caller is responsible for that pumpIndex -> chemical
    // index lookup (main.cpp already has this exact mapping via
    // chemicalIndexForPump(), built earlier for the per-chemical
    // threshold/cap work).
    //
    // Unlike advanceTime()'s per-day rate application, this applies the
    // dose's ENTIRE known effect immediately and once -- a Quick Dose is a
    // single already-fully-delivered mL amount, not an ongoing daily rate.
    // Feeds the exact same pendingContribution ledger AI-planned doses
    // use, so this manual action gets properly credited/blamed by the
    // confidence-learning step just like the AI's own dosing would be --
    // manual intervention becomes real training data instead of an
    // invisible confound.
    void recordManualDose(int chemicalIndex, float mlDosed);

    // Target ranges are Recommendable too (§1/§7) — customer-adjustable
    // within safe bounds, tank-scaled defaults via TankEstimator.
    Recommendable targetAlkDkh, targetPhLow, targetPhHigh, targetCaPpm, targetMgPpm;

    // Call on every new test result or scheduled epoch (§7 architectural
    // separation — this is the "slow matrix recalculation," never called
    // from the pump-timing task).
    DosingPlanV2 recalculate(bool lightsActive, float currentPh);

    // Call when a real measurement arrives (manual entry or Apex/Trident).
    // Assumes the caller has already de-duplicated repeat/unchanged
    // readings (§4.1: "must trigger strictly on genuinely new data") --
    // this class has no visibility into polling cadence itself, that
    // discipline lives in the caller (main.cpp).
    //
    // Added 2026-08-04: as of now, this also runs the confidence-learning
    // step (see pendingContribution's comment above) BEFORE the Kalman
    // update overwrites the filter's predicted level -- comparing what
    // actually happened to what the currently-declared chemicals' dosing
    // should have produced, and adjusting their confidence accordingly.
    void ingestMeasurement(WaterParam p, float value, MeasurementSource source);

    // §4.2 Week layer: real trend over the trailing ~7 real-world days,
    // computed from actual logged measurements (not extrapolation). A
    // simple two-point slope between the oldest and newest measurement
    // still inside the window -- matches §4.5's own "start simpler"
    // staged-build philosophy rather than a full regression. Returns 0.0f
    // if fewer than 2 real measurements exist inside the window (nothing
    // to compute a trend from yet) -- check hasWeekData() first if the
    // caller needs to distinguish "genuinely flat" from "not enough data."
    float getWeekTrend(WaterParam p) const;
    bool hasWeekData(WaterParam p) const;

    // §4.3 Month layer: same mechanism, ~30-day window. Comparing this
    // against getWeekTrend() is how drift gets detected (see
    // isDrifting() below) -- a real, sustained difference between the
    // short and long window is what "demand has been climbing for
    // weeks" actually looks like, in a way one blended Kalman trend
    // value alone can't reveal.
    float getMonthTrend(WaterParam p) const;
    bool hasMonthData(WaterParam p) const;

    // True if the week-window trend and month-window trend disagree by
    // more than a reasoned threshold -- §4.3's actual drift signal.
    // Deliberately conservative (a real, sustained divergence, not any
    // difference at all) since week and month trends will almost never
    // match exactly even with zero real drift, just from ordinary
    // measurement noise -- kDriftThreshold below is a reasoned starting
    // point, not fleet-validated, same caveat as this file's other
    // unvalidated constants.
    bool isDrifting(WaterParam p) const;

    // §4.4: true if the most recent real measurement for this parameter
    // was flagged as an anomaly by updateConfidenceFromOutcome (see that
    // function's comment in the .cpp) -- cleared on the NEXT measurement
    // for this parameter regardless of that one's own outcome, so this
    // only ever reflects the single most recent result, not a lingering
    // state.
    bool wasAnomaly(WaterParam p) const { return lastAnomaly[p]; }

    // Added 2026-08-04: needed for a real percentage progress bar on the
    // dashboard (hasWeekData/hasMonthData alone only give a binary
    // ready/not-ready, not "how far along"). Returns the span, in days,
    // between the oldest and newest real measurement currently in the
    // buffer -- the dashboard computes its own percentage from this
    // (min(1.0, spanDays/7) for week, /30 for month) rather than this
    // class hardcoding a percentage tied to one specific window length.
    float historySpanDays(WaterParam p) const;

    // Call periodically (independent of measurements) to advance the
    // predict step between tests.
    void advanceTime(float dtDays);

    // §5.2 chemical inventory change handling.
    void addChemical(const ChemicalDeclaration& chem);
    bool removeChemical(int index);   // false if this would fail the
                                       // sufficiency check (§5.2)

    // §4.5 persistence — snapshot/restore Kalman + confidence state across
    // reboots. Not optional polish: ESP-DSP's own docs call this out, and
    // reefDoser3's reboot history (rate limits/crashes/power loss) confirms
    // why — without this, every reboot silently reverts a mature tank back
    // to "new tank" behavior (§8).
    //
    // Returns false on any failure (no saved entry yet, storage error, or
    // corrupt/version-mismatched blob). Caller should treat false from
    // restoreState() exactly like first boot — safe defaults (§8) — never
    // partially trust a failed restore. Persistence failure is non-fatal to
    // dosing but should be logged by the caller (this class doesn't take a
    // Logger dependency).
    //
    // Call saveState() after ingestMeasurement/addChemical/removeChemical
    // (meaningful updates). Do NOT call it from every advanceTime() predict
    // step — that runs far more often and would cause needless flash wear
    // for no new information.
    bool saveState();
    bool restoreState();
};

// -----------------------------------------------------------------------
// §4.5 persistence schema (NVS-backed, see AI_EngineV2.cpp). Scope is
// deliberately narrow — only state the learning system produces, not
// customer-declared configuration (that already persists elsewhere).
// schemaVersion exists so a future field change can detect and reject an
// old-format blob (restoreState() returns false, falls back to §8 defaults)
// instead of misreading it.
// -----------------------------------------------------------------------
struct PersistedChemicalConfidence {
    char name[24] = {0};                    // matched against ChemicalDeclaration::name
    float confidence[kNumParams] = {0, 0, 0, 0};
};

struct PersistedStateV1 {
    static constexpr uint32_t kMagic = 0xA1E2C0DEu;
    static constexpr uint16_t kSchemaVersion = 6; // bumped 2026-08-04: added
    // history[]/historyNextSlot[]/totalElapsedDays (§4.2/§4.3 Week/Month
    // trend buffer) as new fields. Found and fixed as a real, direct gap:
    // this buffer was originally session-only, but reefDoser3's own boot
    // history shows well over a dozen reboots in a single night (OTA
    // pushes, crashes, power cycles) -- a 7/30-day rolling window that
    // resets on every single reboot could realistically never accumulate
    // enough real data to ever report anything at all on a device that
    // reboots this often. Same non-negotiable persistence requirement
    // this file's own comment already states for the Kalman filter state
    // itself applies here too: "repeated reboots... must not silently
    // reset the system back to 'new tank' behavior." An old pre-v6 blob
    // has no history data to lose -- restoreState() rejecting it and
    // starting the Week/Month buffer fresh is the correct, honest outcome
    // for a genuinely new field, not a regression.

    uint32_t magic = kMagic;
    uint16_t schemaVersion = kSchemaVersion;
    ParamKalmanState filters[kNumParams];
    uint8_t numConfidenceEntries = 0;
    PersistedChemicalConfidence confidenceByName[kMaxChemicals];
    // Added 2026-08-04, see schemaVersion comment above.
    AIEngineV2::HistoryEntry history[kNumParams][AIEngineV2::kHistoryCapacity];
    int historyNextSlot[kNumParams] = {0, 0, 0, 0};
    float totalElapsedDays = 0.0f;
    // CRC32 is stored as a separate NVS key alongside this blob (see .cpp),
    // not as a member here, so the checksum never covers itself.
};