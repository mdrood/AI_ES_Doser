# reefDoser3: Removing the Mode Picker — Full Plan

## Why this exists

The spec's §5 vision was "customer declares what they're dosing, no more mode
picker." The AI_Engine v2 migration delivered an engine that genuinely
doesn't know what a "mode" is (`AIEngineV2::addChemical`/`removeChemical`
take any declared chemical). But `main.cpp` and `Dashboard.h` still run on
`dosingMode` (1-8) end to end:

- `main.cpp`: `dosingMode` int, `switch(dosingMode)` in
  `pumpKeyForPhysicalIndex()`, a `SlotSpec[6]` table in
  `rebuildAiChemicalDeclarations()` keyed to it.
- `Dashboard.h`: a literal `<select id="dosingModeSelect">` with 8 hardcoded
  options, AND a **second, independently hand-maintained copy** of the same
  mode→pump→chemical-name table (`DOSING_MODES`, JS) used by the pump
  selector, calibration screen, and active-plan display.

Two hardcoded copies of the same wiring table, in two languages, that must
be kept in sync by hand — a real drift risk on its own, on top of the
"modes vs. free declaration" gap the spec wanted closed.

## Hard constraints (confirmed from the actual code, not assumed)

- **Exactly 4 physical pumps.** `SAFETY_PUMP_PINS[4] = {22, 25, 26, 27}` —
  fixed GPIO wiring, not a mode artifact. Any new design still caps
  "actively pumping" chemicals at 4.
- **Up to 8 declared chemicals.** `kMaxChemicals = 8` in the engine itself —
  more than the 4 pumps, meaning a customer could pre-declare a chemical's
  identity/potency before it's physically wired to a pump (or after
  unwiring it, without losing the configuration). Worth deciding whether to
  expose this distinction (see Open Questions).
- **LittleFS has room.** ~2MB total, ~70-90KB currently used (from actual
  boot logs) — a new small JSON config file costs nothing meaningful.
- **Existing API surface to build alongside, not replace outright, during
  the transition:** `/api/mode` (local OFF/AUTO/MAN — unrelated, keep as
  is), `/api/dosing-mode` (the thing being replaced), `/api/config/
  chemical-strengths`, `/api/config/safeties`, `/api/calibration`,
  `/api/chemical-levels` (manual test entry), `/api/config/ai-baseline`.

## Target design

### Data model — one declared chemical

Maps directly onto the engine's own `ChemicalDeclaration`, so
`rebuildAiChemicalDeclarations()` becomes close to a direct load, not a
derivation:

```
{
  id: string,                // stable id, not array index (survives reorder)
  name: string,               // customer-facing label
  presetId: string | null,    // references ChemicalPresets.h's table, or null = custom
  potencyAlkPerMl: float,     // dKH/mL — auto-filled from preset, editable if custom
  potencyCaPerMl: float,      // ppm/mL
  potencyMgPerMl: float,      // ppm/mL
  phSensitive: bool,
  pumpIndex: int,             // 0-3, or -1 = declared but not physically wired
  maxMlPerDay: float,         // §7 safety cap
  active: bool
}
```

### Persistence: one LittleFS JSON file, not scattered NVS keys

`/config/chemicals.json` — an array of the above. Chosen over per-field NVS
keys because: variable-length (0-8 entries), ArduinoJson is already a
dependency, and this matches the existing pattern for structured data on
this device (`alkDemandHistory`/`calciumDemandHistory` already use LittleFS
files, confirmed in boot logs: `ALK 7-DAY INIT: file=found`).

### New backend API (WebRoutes)

- `GET /api/chemicals` — full declared list.
- `POST /api/chemicals` — add one (name + either `presetId` or custom
  potency values + pumpIndex).
- `POST /api/chemicals/update` — edit an existing entry by id.
- `POST /api/chemicals/remove` — remove by id. Runs the **same §5.2
  sufficiency check `AIEngineV2::removeChemical` already does** as a trial
  before committing; returns the failure explanation string if insufficient
  instead of silently allowing a gap. Hard-blocks by default (see Open
  Questions if you want an override path instead).
- `GET /api/chemical-presets` — serializes `kChemicalPresets` to JSON. This
  is the fix for the "hardcoded table in two languages" problem: the
  dashboard's preset dropdown is populated from this at runtime, so there
  is exactly ONE source of truth (the C++ table), not a third hand-copied
  version in JS.

### `main.cpp` changes

- Remove: `dosingMode` variable, `pumpKeyForPhysicalIndex()`,
  `physicalPumpIndexForLegacySlot()`, `SlotSpec[6]`, `isValidDosingMode()`.
- Rewrite `rebuildAiChemicalDeclarations()`: load `/config/chemicals.json`,
  populate `ChemicalDeclaration[]` directly — actually *simpler* than
  today's version, since there's no mode-indirection left to resolve.
- Rewrite `syncLegacyPlanFromV2()` (maps `DosingPlanV2::mlPerDay[]` back to
  physical pump actuation): read `pumpIndex` straight off each declared
  chemical instead of re-deriving it from the old mode table.

### Migration (existing customers keep working, don't lose config)

One-time, at boot: if `/config/chemicals.json` doesn't exist yet AND a
legacy `dosing_mode` NVS value does, auto-generate the equivalent declared
list from the customer's CURRENT `dosingMode` + their existing chemical-
strength values (reusing today's `SlotSpec` construction logic one last
time, as a one-shot converter, then writing the result to the new file).
From then on, the new file is authoritative; legacy `dosing_mode`/`SlotSpec`
fields become dead weight, removed in Phase 3 once this is proven.

### Dashboard changes

- **Remove:** `<select id="dosingModeSelect">`, `saveDosingMode()`,
  `DOSING_MODES` (the whole hardcoded JS table).
- **Add:** a "Manage Chemicals" section — dynamic cards, one per declared
  chemical: name (editable), preset dropdown (populated from
  `GET /api/chemical-presets`, "Custom" as an option), potency fields
  (auto-filled from preset, editable only if Custom), pump-assignment
  dropdown (Pump 1-4, or "Unassigned"), active toggle, Remove button
  (surfaces the sufficiency-check rejection inline if the backend blocks
  it), "Add Chemical" button.
- **Rewrite to read the new list instead of `getModeCfg(mode)`:**
  `renderPumpSelect`, `renderCalibration`, `renderActivePlan`,
  `pumpChemicalName` — all four currently key off the mode table and need
  to key off `GET /api/chemicals` instead.
- **Fold in, don't duplicate:** the existing "recipe" section (chemical
  type/source pickers like "Tropic Marin AFR Powder Standard," "BRS
  Recipe") already does roughly what a preset picker should — this becomes
  the preset dropdown inside each chemical card, not a separate parallel
  UI living next to it.

## Correction, found while starting Phase 1: a THIRD hardcoded copy exists,
## and it's the most safety-critical one

Digging into `syncLegacyPlanFromV2()` revealed it maps `DosingPlanV2` by
**named chemical identity** (`currentPlan.kalk/afr/alk/cacl2/naoh/mg`), not
by physical pump. That's then consumed by the **AI bucket scheduler**
(`pumpBuckets[N] += currentPlan.<name> / 144.0f`, inside a
`switch(dosingMode)` block with one case per mode) — and `pumpBuckets[]` is
what actually accumulates real dosing volume that triggers a pump. This is
the real execution path, not a display/reporting detail — meaning **it
cannot be deferred to Phase 3**. A freely-declared chemical list literally
dosing incorrectly (or not at all) without this rewritten, since the switch
statement has no case for anything beyond the 8 fixed modes.

Good news: replacing it is a simplification, not just a change swap. Once
`pumpIndex` lives directly on each declared chemical (already planned),
the entire 8-case switch + 6 named intermediate fields collapse to:

```cpp
for (int c = 0; c < declaredChemicalCount; c++) {
    int pumpIdx = declaredChemicals[c].pumpIndex;
    if (pumpIdx >= 0 && pumpIdx < 4) pumpBuckets[pumpIdx] += plan.mlPerDay[c] / 144.0f;
}
```

**Knock-on effect, needs a decision:** `currentPlan`'s named fields are
also read for local status JSON and Firebase mirroring
(`json.set("plan/kalk", ...)` etc.) — reporting that assumes chemicals are
always named kalk/afr/alk/cacl2/naoh/mg. Once a customer can name their own
chemicals anything, that assumption breaks. Two options:
- **(a)** Report plan by pump index instead (`plan/pump1`..`plan/pump4`),
  update the dashboard's `planFromStatus`/`aliasKeysForPlan` to match.
- **(b)** Report plan by the new declared-chemical id/name
  (`plan/chemicals/<id>`), richer but a bigger dashboard change.

Recommending **(a)** for Phase 1/2 — simpler, and pump index is already a
stable concept the dashboard understands (calibration screen already keys
off physical pump index). Revisit (b) later if useful.

## Phase 1 scope, corrected — DONE (2026-07-24)

**Phase 1 — Backend only, dashboard untouched and still working.**
New persistence schema + load/save, new API endpoints, migration-on-boot,
`rebuildAiChemicalDeclarations()` rewrite, **and the bucket-scheduler
rewrite** (the `switch(dosingMode)` block feeding `pumpBuckets[]] — this is
the actual dosing-execution path, confirmed non-deferrable, see correction
above). Also updates local status JSON to report by pump index (`plan/
pump1..4`) instead of by legacy chemical name, per option (a) above. Keep
`/api/dosing-mode` alive as a compatibility shim during this phase
(translates an incoming mode number into the equivalent declared list
under the hood, preserving its existing Firebase-mirroring behavior
unchanged) so the *current* dashboard keeps functioning unmodified while
Phase 2 is built. Test via direct API calls (curl/Postman-style) before
touching any UI: add, edit, remove (including a forced sufficiency-check
rejection), list, presets, and confirm a pump actually receives the
correct accumulated bucket volume for a freely-declared (non-legacy-named)
chemical.

**What was built:**
- `WebRoutesShared.h`: `DeclaredChemical` struct, `kMaxDeclaredChemicals=4`,
  extern globals, extern declarations for all new shared functions.
- `main.cpp`: `declaredChemicals[]`/`declaredChemicalCount` (the real
  globals), `planMlPerDayByIndex[]` (mode-agnostic replacement for reading
  `currentPlan`'s named fields in the execution path), `loadDeclaredChemicals()`/
  `saveDeclaredChemicals()` (`/chemicals.json` on LittleFS, write-temp-then-
  rename for power-loss safety, matching this file's existing
  `boot_history.log` pattern), `buildDeclaredChemicalsFromLegacyMode()`
  (reuses `pumpKeyForPhysicalIndex()` rather than inventing a fourth copy
  of the wiring table), rewritten `rebuildAiChemicalDeclarations()`
  (index-aligned with `ai.chemicals[]` by construction — see its own
  comment on why that correspondence is load-bearing), rewritten
  `syncLegacyPlanFromV2()` (populates `planMlPerDayByIndex[]`, plus
  best-effort NAME-matched legacy mirroring for the transition), rewritten
  bucket scheduler (8-case `switch(dosingMode)` → one mode-agnostic loop
  over declared chemicals' `pumpIndex`), migration-on-boot call in
  `setup()`, and pump-indexed Firebase mirroring (`plan/pump1..4`)
  alongside the legacy named mirror.
- `WebRoutes.cpp`: five new handlers (`GET/POST /api/chemicals`,
  `POST /api/chemicals/update`, `POST /api/chemicals/remove`,
  `GET /api/chemical-presets`), all registered in `registerWebRoutes()`.
  `/api/dosing-mode`'s handler updated to also call
  `buildDeclaredChemicalsFromLegacyMode()` so the old mode picker and the
  new declared list stay consistent during the transition.

**Testing status: COMPLETE (2026-07-24), all passing on real hardware
(reefDoser3):**
1. ✅ Migration ran correctly on first boot after flashing — serial log
   showed `CHEMICAL MIGRATION: built 3 declared chemicals from legacy
   dosingMode=8`, `/chemicals.json` created with the correct 3 entries
   (Kalkwasser/CaCl2/NaOH on pumps 0/1/2, matching every existing
   `maxMlPerDay`/potency value exactly).
2. ✅ `GET /api/chemicals` — listed all 3 migrated chemicals correctly.
3. ✅ `GET /api/chemical-presets` — all 9 presets listed with correct
   `movesAlk`/`movesCa`/`movesMg`/`phSensitive` flags, including the two
   multi-parameter presets (All-For-Reef, Kalkwasser).
4. ✅ `POST /api/chemicals` on an already-occupied pump (0) — correctly
   rejected with the pump-uniqueness error, no hardware conflict created.
5. ✅ `POST /api/chemicals` on the free pump (3) with a preset — correctly
   auto-filled potency from `BRS 2-Part - Magnesium` (0.00725 ppm/mL,
   exactly matching 7.25 ppm/mL/gal ÷ 1000 gal), correct `pumpIndex`,
   correct default `maxMlPerDay` from the pump's existing cap.
6. ✅ `POST /api/chemicals/remove` on that same test entry — removed
   cleanly, state correctly returned to the original 3 chemicals.

One cosmetic fix made during this testing: `saveDeclaredChemicals()`
unconditionally called `LittleFS.remove()` before writing, which logged a
harmless but noisy `[E]` VFS error on the very first save (nothing to
remove yet). Now guarded with `LittleFS.exists()` first.

Bucket-scheduler / real dosing behavior over a full cycle has NOT been
separately re-verified beyond the boot log showing it started correctly
(`AI BUCKET SCHEDULER STARTED`) — worth a longer-running check before
fully trusting this on an unattended tank, same caution as any other
change to the dosing-execution path.

## Phase 2 — Dashboard UI — DONE (2026-07-24, not yet hardware-tested)

**What was built:**
- Removed the `<select id="dosingModeSelect">` mode picker card entirely,
  replaced with a "Manage Chemicals" card: lists each declared chemical
  (name, pump, preset/custom, potency, active/disabled badge), with
  Enable/Disable and Remove buttons per row, and an "+ Add Chemical" form
  (name, preset dropdown populated from `GET /api/chemical-presets`,
  custom potency fields shown only when "Custom" is selected, pump
  dropdown showing only currently-free pumps).
- `getModeCfg(mode)` rewritten: `mode` parameter kept (ignored) for
  call-site compatibility, but pump wiring now comes entirely from
  `declaredChemicalsCache` (fetched via `GET /api/chemicals`), sorted by
  pump index. This means `renderPumpSelect`, `renderCalibration`,
  `renderActivePlan`, and `pumpChemicalName` — all four of the
  mode-dependent functions flagged back when this gap was first found —
  needed **zero changes of their own**, since they all go through
  `getModeCfg()`.
- `planFromStatus()` now prefers the new `dosingMlPerDayByPump` field
  (added to `/api/status` in `WebRoutes.cpp`, pump-indexed and correct for
  any freely-named chemical) over the legacy `dosingMlPerDay` object,
  which only ever worked for the six hardcoded names.
- `declaredChemicalsCache`/`chemicalPresetsCache` loaded once at startup
  (not on the 5-second poll — this data only changes via explicit
  add/edit/remove, which already reload it themselves after committing).
- `DOSING_MODES` and `saveDosingMode()` removed outright (not just hidden)
  — confirmed safe since Phase 1's API was already hardware-tested
  end-to-end before this was built.

**Bug caught and fixed during this work, worth flagging:** the *original*
`Dashboard.h` (as uploaded, before any of this migration) had two
independent definitions of `getModeCfg()` — an old one-line version
further down in the file, and the "real" one used everywhere. JS lets a
later function declaration silently shadow an earlier one in the same
scope, so after rewriting the first `getModeCfg()`, the second, unrelated,
still-`DOSING_MODES`-referencing one would have silently taken over at
runtime and thrown a `ReferenceError` on first use — every pump-related
render on the page would have broken. Found by grepping for the function
name after the edit and noticing two hits; removed the dead duplicate.
Worth being aware this kind of shadowing bug can hide in existing code
independent of anything this migration touched.

**Not yet done:**
- Not tested on real hardware yet — this needs a browser hitting the
  actual dashboard, not curl. Load the page, confirm the Manage Chemicals
  card renders the 3 migrated chemicals correctly, try adding/removing
  through the UI (not just the API), confirm the Active Plan / Calibration
  / quick-dose pump selector cards all still populate correctly now that
  they're reading from the new source.
- No dedicated "edit an existing chemical's potency/preset" UI yet — the
  backend (`POST /api/chemicals/update`) supports it fully, but the
  current UI only exposes Enable/Disable and Remove per row, not a full
  edit form. Worth adding if you want to change a chemical's potency
  without removing and re-adding it.
- Phase 3 (removing now-dead legacy code: `SlotSpec`'s remnants,
  `pumpKeyForPhysicalIndex`, the `/api/dosing-mode` shim, `dosingMode`
  itself) has not been started — intentionally, since the shim is still
  useful as a rollback path until the new UI is proven on real hardware.

## Decisions locked in (2026-07-24)

1. **Sufficiency-check on remove: hard block.** Matches today's
   `AIEngineV2::removeChemical` behavior exactly — no new engine-side work
   needed here, just surface the existing rejection reason in the API
   response.
2. **Dashboard/local-network only, no cloud path.** Simplifies Phase 1 —
   no need to build a Firebase-side mirror of the new chemical list, no
   `/settings/chemicals`-style cloud listener to add. `/settings/
   dosingMode`'s cloud listener can be removed outright in Phase 3 rather
   than replaced with an equivalent.
3. **Always require a pump assignment at creation — no "unassigned"
   state.** Simplifies both the data model (no `pumpIndex = -1` case to
   handle downstream) and the UI (no separate "declared but not wired"
   visual state). A chemical can still be edited later to change which
   pump it's on, or deleted; it just can't exist mid-air with no pump.
   `kMaxChemicals = 8` on the engine side is now effectively capped at 4 in
   practice (one per physical pump) — worth a one-line note in code, not a
   design problem.

   **Falls out of this:** since each physical pump can only run one
   chemical at a time (no hardware sharing), `pumpIndex` must be unique
   among declared chemicals — `POST /api/chemicals` and
   `/api/chemicals/update` both need to reject a pump assignment already
   held by a different chemical, not just accept it and silently create a
   hardware conflict.
