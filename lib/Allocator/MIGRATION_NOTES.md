# AI_Engine v1 → v2 Migration — Stage 1

## What this delivers

The architectural pieces the spec has fully resolved, working end to end:

| Legacy v1 (`AI_Engine.cpp`) | v2 replacement | Spec section |
|---|---|---|
| `mode` int (1–8), 8-mode switch | `ChemicalDeclaration[]` — customer declares what they have | §5 |
| Per-mode hardcoded kalk/naoh/alk share math | `Allocator::solve()` — NNLS constrained optimization | §5.1 |
| `mode7Split.naohMaxPh` hard cutoff | `Allocator::phDerateWeight()` — continuous taper | §5.1 refinement |
| `fastAlk` two-speed boost/step ladder (Alk only) | `MathEngine` Kalman filter, maturity-gated step size (all 4 params) | §4.5, §4.4 |
| Stacked override layers (`applyAllModeTwoSpeedAlkRecovery` → `applySafetyEnforcement` → `addBaselineDemand` → `applyNaohPhCaution` → Mode 7/8 rescue floors → `applyAbsoluteCaps`) | One solve, one precedence order (§5.1 comment block in `Allocator.h`) | §7 "one clear precedence order" |
| Fixed 300G reference + volume scale only | `TankEstimator::scale()` — volume *and* coral-load scale, same function proposed for baselines/caps/targets | §6/§7, Core Design Principle |
| No override-protection — dashboard caps could be silently re-suggested over | `Recommendable` struct — every customer-facing number remembers whether it was overridden | Core Design Principle |
| `mode` param entirely (no chemical add/remove flow) | `AIEngineV2::addChemical/removeChemical` with §5.2 sufficiency check | §5.2 |
| Nothing (customer manually entered strengths for everything) | `ChemicalPresets.h/.cpp` — BRS 2-Part, ESV B-Ionic, Tropic Marin All-For-Reef auto-fill from published specs | §5 |

**The specific bug named in the spec is fixed by construction, not patched.**
v1's Mode 7 problem (`mode7Split` day/night percentages vs. the NaOH rescue
floor silently fighting each other, `AI_Engine.cpp` lines ~202–354) was
structural: "priority" was expressed as "whichever adjustment runs later in
the function." v2 has no later function that can reopen a decision — day/night
and pH conditions are baked into the cross-effect matrix *before* the single
NNLS solve, and the only scaling step after that is the uniform safety-cap
scale-down, applied once, per §7.

## What is intentionally NOT filled in here (real spec gaps, §10)

I did not invent numbers for these — they need your input before this ships:

1. **Coral-load multiplier table** (`TankEstimator::scale`) — resolved
   2026-07-23 into **two separate tables**, gated by a new `Purpose`
   parameter (`BaselineDemand` vs. `SafetyCap`), instead of one shared
   number:
   - `BaselineDemand`: 0.60 / 1.00 / 1.40 / 2.20 (Light/Moderate/Heavy/SPS).
     Wider spread is safe here because this estimate is corrected over time
     by the Kalman learner (§4.5) once real per-tank data comes in during
     the Observation/Break-in Period (§8) — it only needs to be a
     reasonable starting guess, not a precise number.
   - `SafetyCap`: 0.75 / 1.00 / 1.20 / 1.50 — deliberately compressed.
     Owner decision (2026-07-23): err conservative/tight here, even at the
     cost of slower correction on heavy/SPS tanks, because §7 explicitly
     forbids the learning system from ever adjusting the cap — it's the
     one number in this system that doesn't get a second chance to be
     right.
   Neither table is fleet-validated yet (no telemetry available to
   calibrate against per owner, 2026-07-23) — revisit once reefDoser fleet
   data (§9) exists.
2. **Commercial preset table** (§5) — **implemented** 2026-07-23 in the new
   `ChemicalPresets.h`/`.cpp`. Owner confirmed the three product lines to
   support at launch: BRS 2-Part, ESV B-Ionic, Tropic Marin All-For-Reef.
   Potency values are derived from each manufacturer's own published dosing
   specs (not fleet-measured) — two derivation paths, cited per entry in
   the code: (a) manufacturer states the tank effect directly (ESV, BRS
   Alk/Ca), used as-is; (b) manufacturer states only bottle concentration
   (All-For-Reef) or nothing at all (BRS Magnesium), converted via the
   standard dilution formula or raw stoichiometry respectively. The
   dilution formula was verified by reproducing ESV's own stated 2.07 dKH
   figure exactly from their published concentration before trusting it
   for All-For-Reef, and the All-For-Reef result independently landed
   within ~10% of real hobbyist-reported dosing. `presetToDeclaration()`
   converts a preset + the customer's declared tank volume into a ready
   `ChemicalDeclaration`, verified with a standalone harness (tank-volume
   scaling, exact manufacturer-figure reproduction, pH-sensitivity flags,
   confidence seeding, invalid-input handling). Deliberately NOT set by
   this function: `maxMlPerDay` (§7 safety cap — a different estimator
   input) and `.active` — wizard/dashboard work, same scope boundary as
   the rest of Stage 1. Not fleet-validated — same caveat as every other
   estimate in this migration; a candidate to revisit once real dosing
   response data exists per product.

   **Added 2026-07-23, same day, owner request:** three DIY/raw-chemical
   presets not tied to a brand — sodium hydroxide (NaOH), sodium
   bicarbonate (baking soda), and kalkwasser (calcium hydroxide). NaOH and
   bicarbonate derived via recipe-stated concentration + independent molar-
   stoichiometry cross-check (within ~1-1.5% agreement). NaOH is the
   literal chemical v1's `naohMaxPh` gate was named for. Sodium bicarbonate
   is deliberately marked `phSensitive = false`, not a default/oversight —
   multiple sources describe it as mildly pH-*lowering*, the opposite
   direction from soda ash/NaOH, which is why hobbyists use it specifically
   on high-pH tanks; gating it with the ceiling-taper logic built for
   pH-raising chemicals would misapply that mechanism, not just skip an
   unnecessary safeguard. Kalkwasser is the only preset that moves both Alk
   and Ca from a single dose (alongside All-For-Reef) — no manufacturer
   states one clean tank-effect figure for it, so its numbers are an
   average of three independent hobbyist-reported dosing outcomes
   (agreeing within ~10%); the resulting Ca:Alk ratio (7.39 ppm/dKH)
   independently lands near the ~7.1 ratio corals actually consume these
   in, which is kalkwasser's well-known "self-balancing" property — a
   reassuring but not conclusive consistency check, still not fleet data.
3. **Persistence schema** (§4.5 "critical requirement") — **implemented**
   2026-07-23. `AIEngineV2::saveState()`/`restoreState()` in `AI_EngineV2.cpp`,
   backed by NVS (`Preferences`), namespace `aiengine2`. Scope is
   deliberately narrow — only learned state (4 Kalman filters + per-chemical
   confidence), not customer-declared config, which already persists
   elsewhere. Confidence is matched by chemical **name** on restore, not
   array index, so an inventory edit between reboots (§5.2) only affects the
   changed chemical's slice. Versioned (`schemaVersion`) and CRC32-checked;
   any failure (first boot, corruption, version mismatch) returns `false`
   and the caller falls back to §8 safe defaults rather than trusting a
   partial read. Verified against 5 scenarios (round-trip, reordered/new
   chemical, first boot, corrupted write) with a standalone harness — see
   commit notes. Caller still needs to wire up *when* to call `saveState()`
   (after `ingestMeasurement`/`addChemical`/`removeChemical`, not on every
   `advanceTime()` tick — flash wear) — that's `main.cpp` integration, not
   an `AI_EngineV2` concern.
4. **Measurement-noise magnitudes** per test method (`measurementNoiseFor`)
   — resolved 2026-07-23, and a real bug fixed along the way, not just an
   unconfirmed magnitude filled in: the old signature took only `source`
   and applied one flat noise value regardless of *which* parameter it was
   updating — wrong-sized for at least three of the four (Alk ~8 dKH scale,
   pH ~8.2, Ca ~450 ppm, Mg ~1400 ppm all sharing one number). Now
   `measurementNoiseFor(WaterParam, MeasurementSource)`, with values
   grounded in known reef test-kit precision (Trident's automated titration
   and Hanna's digital colorimeters tighter than manual titration/color kits;
   Mg manual titration is the hobby's most notoriously imprecise common
   test). Trident < Hanna < Manual ordering holds within every parameter,
   verified with a standalone harness. Still not fleet/lab-calibrated to
   this product's actual sensors — §10 "test-method disagreement" remains
   open for that final tuning — but the shape and units are correct now,
   not just the ordering.
5. **pH taper band width** (`Allocator::phDerateWeight`) — **resolved**
   2026-07-23, not a fleet-data question. Kept at 0.30, but now derived and
   documented rather than an unjustified constant: §1's pH target range
   tops out at 8.40, the allocator's fixed safety ceiling is 8.60 (a 0.20
   gap), and 1.5x that gap gives 0.30 — throttling starts at pH 8.30
   (inside the target range) and NaOH is already down to 75% strength by
   the time the tank reaches the top of its target range. If the target
   range or ceiling values ever change, re-derive from the same 1.5x ratio.

## What's deliberately out of scope for Stage 1

- **Execution engine / FreeRTOS pump task.** Untouched — `Doser` lib stays as
  is. §7 requires this separation; `AIEngineV2::recalculate()` only produces
  `mL/day` recommendations, same contract v1's `currentPlan` had.
- **Wizards (§8/§8.5).** Setup flow, required baseline test gating, and
  guided check-ins are dashboard/WebRoutes work, not AI_Engine.
- **NNLS solver is a compact Lawson-Hanson implementation**, matching the
  spec's stated "~100 lines, no heavy library" — production should still
  validate it against the synthetic digital-twin simulator (§9.5) before
  trusting it on a real tank, same as the Kalman filter.
- **Kalman filter is Stage 1 (scalar, level+trend) per the spec's own staged
  recommendation** — §4.5 explicitly calls for starting here before the full
  cross-effect EKF (ESP-DSP/TinyEKF). Swap-in point is `MathEngine::predict/update`.
- **ETL fixed-capacity containers** — the spec recommends ETL for the
  variable-length chemical list on real hardware. This stage uses plain
  fixed-size arrays (`kMaxChemicals = 8`) for portability while validating
  logic; swapping to `etl::vector` is a mechanical follow-up, not a design change.

## Suggested next steps

All five of the original Stage-1 open items are now resolved (coral-load
multipliers, persistence, pH taper band, measurement noise, preset table —
see numbered list above). What's left is integration, not open design
questions:

1. Wire `AIEngineV2` into `main.cpp` alongside (not replacing) the v1 engine,
   per the spec's "v2 is a parallel, next-generation system" framing —
   existing customers stay on v1's 8-mode system.
2. Build the synthetic digital-twin simulator (§9.5) and validate the NNLS
   solver + Kalman filter against known ground truth before pointing either
   at a real tank's `ApexLogEmulator` replay.
3. Persistence is implemented but not yet *called* from anywhere — wire
   `saveState()` into `main.cpp` after the relevant mutating calls, and
   `restoreState()` once at boot, before this survives a reboot in the
   field.
4. None of the manufacturer-derived numbers in this file (coral-load
   multipliers, preset potencies, measurement noise) are fleet-validated
   yet — real dosing-response data should supersede all of them over time,
   same as the spec's own §9 fleet-learning plan anticipates.

## Bugs found via real device testing (post-Stage-1)

**2026-07-24 — Sufficiency check false-positive on structurally unaddressable
parameters (`Allocator::solve`).** Found on first real boot on hardware:
device reported `sufficient=no` / "insufficient chemical capacity" on every
cycle even though Alk and Ca dosing were working correctly. Root cause: the
sufficiency check (§5.2) treated ANY parameter with a nonzero desired
correction and `achieved < 0.5x desired` as a shortfall — but Mode 8
(`main.cpp`'s `pumpKeyForPhysicalIndex()`) has no Mg pump wired at all, so
`achieved[Mg]` is unconditionally 0 regardless of Alk/Ca health, and any
nonzero Mg desired correction (which measurement noise alone will produce)
permanently tripped `sufficient=false`. This conflated two different
situations: "you have relevant chemicals but not enough of them" (a genuine,
actionable warning) vs. "zero active chemicals can touch this parameter at
all in the current hardware configuration" (a structural fact, not a dosing
shortfall). Fixed by tracking per-parameter whether *any* active chemical
has nonzero potency on it; parameters with none are excluded from
`sufficient` but still surfaced via a distinct note in `plan.explanation`
("[NOTE: at least one parameter has no active chemical able to touch it —
structural, not a dosing shortfall]") so the information isn't lost, just
not conflated with a real shortfall. Verified with a 3-scenario standalone
harness: (1) reproduces the exact reported bug — Mg unaddressable in Mode 8,
Alk/Ca fine — now correctly resolves `sufficient=true`; (2) a genuine Alk
shortfall (desired far beyond Kalk+NaOH combined capacity) still correctly
resolves `sufficient=false`; (3) a genuine Mg shortfall when Mg IS
addressable (a chemical with Mg potency but too small a cap) still correctly
resolves `sufficient=false` — confirming the fix narrows the check rather
than weakening it.

**Separately worth main.cpp's owner's attention, not fixed here:** since
Mg is confirmed intentionally undosed in Mode 8, `runAiRecalculation()`
still calls `ai.ingestMeasurement(P_MG, ...)` and sets a Mg target every
cycle regardless — harmless now that the above fix is in, but worth
considering whether Mode 8 should skip Mg targeting/ingestion entirely
rather than relying on the Allocator to absorb it silently.
