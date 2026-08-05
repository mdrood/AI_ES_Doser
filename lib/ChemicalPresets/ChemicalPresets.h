#pragma once
#include "AI_EngineV2.h"

// =============================================================================
// §5 Named commercial chemical presets ("system already knows typical
// composition/potency for that product, auto-fills strengths").
//
// Product lines supported at launch, per owner (2026-07-23): BRS 2-Part,
// ESV B-Ionic, and Tropic Marin All-For-Reef. Three DIY/raw-chemical
// entries added 2026-07-23 at owner's request: sodium hydroxide (NaOH),
// sodium bicarbonate (baking soda), and kalkwasser (calcium hydroxide) —
// all extremely common, well-documented stand-ins not tied to one brand.
//
// Potency values are derived from each manufacturer's own published dosing
// specifications — not fleet-measured, so treat these the same as every
// other estimate in this migration (§10): a real, defensible starting
// point, not a confirmed number. Two derivation methods used, both cited
// per entry below:
//   (a) Manufacturer states the tank effect directly ("1 mL per gallon
//       raises Alk by X dKH") — used as-is.
//   (b) Manufacturer states only the bottle's own concentration — the
//       standard dilution relationship converts it:
//         potency_per_mL_per_gallon = bottle_concentration_per_liter / 3785
//       (3785 mL per US gallon). This is the same math manufacturers
//       themselves use to produce the (a)-type figures — verified by
//       reproducing ESV's own stated 2.07 dKH figure exactly from their
//       stated 7840 dKH/L concentration before trusting this method for
//       the entries that don't have a manufacturer-stated tank-effect
//       number.
//
// IMPORTANT UNIT NOTE: these constants are per-mL-per-GALLON of tank
// volume (the rise from dosing at a 1 mL-per-gallon ratio) — NOT the raw
// per-mL potency ChemicalDeclaration::potencyPerMl expects, which is
// specific to one tank's actual volume. presetToDeclaration() below does
// that conversion using the customer's declared tank size, same
// "same estimator mechanism, tank-size-scaled" pattern as §6/§7's
// baseline/cap estimator (Core Design Principle).
// =============================================================================

enum class ChemicalPresetId : uint8_t {
    BRS_TwoPart_Alkalinity = 0,   // BRS Pharma Soda Ash / "Recipe #1" Part 2
    BRS_TwoPart_Calcium,          // BRS Pharma Calcium Chloride / Part 1
    BRS_TwoPart_Magnesium,        // Randy Holmes-Farley's Mg recipe, as sold by BRS
    ESV_BIonic_Alkalinity,        // Component #1
    ESV_BIonic_Calcium,           // Component #2
    TropicMarin_AllForReef,       // single all-in-one solution
    DIY_SodiumHydroxide,          // NaOH — the chemical v1's naohMaxPh gate was named for
    DIY_SodiumBicarbonate,        // baking soda — gentle/high-pH-tank alternative to soda ash
    DIY_Kalkwasser,               // saturated calcium hydroxide (Ca(OH)2) solution
    // Added 2026-07-25 at owner's request: old Mode 7/8 used a raw DIY
    // Calcium Chloride entry that never made it into this table when V2's
    // preset system was built -- only branded BRS/ESV calcium products
    // existed until now. Two separate entries, not one, because the two
    // common forms have meaningfully different calcium content per gram
    // (owner-confirmed the standard 250g/gallon reference recipe is for
    // the anhydrous form specifically).
    DIY_CalciumChlorideAnhydrous, // CaCl2, no water of crystallization
    DIY_CalciumChlorideDihydrate, // CaCl2*2H2O -- the more common reef-DIY form
    // Added 2026-08-04: closes a real, confirmed gap -- a customer's
    // already-declared "ALK" chemical had no recipe match at all and no
    // real chemistry-sourced preset either, falling back to raw manual
    // potency entry (the same failure category the Kalk investigation
    // exists because of). Appended here, not inserted earlier, so no
    // already-declared chemical's existing presetId (e.g. Kalk's) shifts.
    DIY_AlkalinitySodaAsh,        // sodium carbonate (Na2CO3), the AI Doser
                                   // Standard recipe already referenced
                                   // elsewhere in this codebase (100 g/gal)
    kNumPresets
};

struct ChemicalPresetSpec {
    const char* displayName;
    float potencyPerMlPerGallon[kNumParams];   // see unit note above
    bool  phSensitive;
};

extern const ChemicalPresetSpec kChemicalPresets[(int)ChemicalPresetId::kNumPresets];

// Converts a preset + this tank's declared volume into a ready-to-use
// ChemicalDeclaration. customName lets the wizard show the customer's own
// label (e.g. "My Alk Dose") while keeping the manufacturer potency data;
// defaults to the preset's own display name.
//
// Deliberately NOT set here: maxMlPerDay (§7 safety cap — comes from the
// TankEstimator SafetyCap path, a different estimator input, not this
// product-potency table) and .active. Wiring those together is wizard/
// dashboard work (§8), out of scope for this function same as the rest of
// Stage 1 (see MIGRATION_NOTES.md).
//
// Returns a ChemicalDeclaration with all-zero potency (harmless no-op if
// ever fed into the allocator) if `id` or `tankVolumeGallons` is invalid —
// caller should treat that as a usage error, not silently trust it.
ChemicalDeclaration presetToDeclaration(ChemicalPresetId id, float tankVolumeGallons,
                                         const char* customName = nullptr);
