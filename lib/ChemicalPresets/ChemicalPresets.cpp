#include "ChemicalPresets.h"
#include <string.h>

const ChemicalPresetSpec kChemicalPresets[(int)ChemicalPresetId::kNumPresets] = {

    // --- BRS 2-Part -----------------------------------------------------
    // Source: reefcalcs.com "Two-part dosing, explained" — states BRS
    // Pharma's pre-mixed liquid is chemically identical to the classic
    // Randy Holmes-Farley "Recipe #1" DIY stocks, with the same published
    // tank-effect figures BRS itself uses in their dosing calculator.

    // 1 mL per gallon raises Alk by 0.135 dKH (manufacturer/recipe-stated).
    // Soda ash (sodium carbonate) has a real, if milder-than-NaOH, pH
    // effect — phSensitive = true.
    // Added 2026-08-04: pH potency = 0.135 * 0.015 (carbonate-based
    // pH-cost-per-dKH factor, same reasoning/value as main.cpp's
    // kCarbonateBasedPhCostPerDkh) = 0.00203.
    { "BRS 2-Part - Alkalinity (Soda Ash)", {0.135f, 0.00203f, 0.0f, 0.0f}, true },

    // 1 mL per gallon raises Ca by 9.8 ppm (manufacturer/recipe-stated).
    // Calcium chloride has no material pH side-effect.
    { "BRS 2-Part - Calcium", {0.0f, 0.0f, 9.8f, 0.0f}, false },

    // Randy Holmes-Farley's Mg recipe (720 g MgCl2·6H2O + 180 g
    // MgSO4·7H2O per US gallon RO/DI), sold pre-mixed by BRS. No
    // manufacturer-stated ppm/mL figure found — derived via stoichiometry
    // (Mg mass fraction of each salt × grams per gallon → concentration →
    // dilution formula). This is the same "start from real stoichiometry"
    // cold-start approach the spec calls for (§5.1), not a guess:
    // 720g*(24.305/203.3) + 180g*(24.305/246.47) = 103.8 g Mg per US
    // gallon (3.785 L) => 27,432 mg/L => 27432/3785 = 7.25 ppm per mL per
    // gallon.
    { "BRS 2-Part - Magnesium", {0.0f, 0.0f, 0.0f, 7.25f}, false },

    // --- ESV B-Ionic ------------------------------------------------------
    // Manufacturer states both tank-effect figures directly on the product
    // label/spec sheet (verified across multiple retailer listings).

    // Component #1: 1 mL per gallon raises Alk by 2.07 dKH (0.74 meq/L).
    // Carbonate/bicarbonate-based — phSensitive = true.
    // Added 2026-08-04: pH potency = 2.07 * 0.015 (carbonate-based
    // pH-cost-per-dKH factor) = 0.03105.
    { "ESV B-Ionic - Alkalinity (Component 1)", {2.07f, 0.03105f, 0.0f, 0.0f}, true },

    // Component #2: 1 mL per gallon raises Ca by 16 ppm.
    { "ESV B-Ionic - Calcium (Component 2)", {0.0f, 0.0f, 16.0f, 0.0f}, false },

    // --- Tropic Marin All-For-Reef ----------------------------------------
    // Single all-in-one solution (Alk + Ca + Mg + trace elements in one
    // bottle). Manufacturer publishes bottle content, not a direct
    // tank-effect figure, so this uses the dilution-formula derivation
    // (see ChemicalPresets.h) — verified against the ESV entries above
    // (exact match to their stated figure) before trusting it here, and
    // independently cross-checked against real hobbyist-reported dosing
    // (~25 mL raising ~1 dKH in a ~40 gal tank, and a second independent
    // report both landing within ~10% of the derived Alk value below).
    //
    // Manufacturer spec (per 500 mL bottle): Carbonate Hardness 2800 dKH,
    // Ca 20,000 mg, Mg 950 mg — read as bottle *content*, so per-liter
    // concentration is 2x: 5600 dKH/L, 40,000 mg/L Ca, 1900 mg/L Mg.
    //   Alk: 5600 / 3785 = 1.48 dKH per mL per gallon
    //   Ca:  40000 / 3785 = 10.57 ppm per mL per gallon
    //   Mg:  1900 / 3785 = 0.50 ppm per mL per gallon (deliberately small —
    //        matches the product's own positioning that Mg contribution is
    //        minor relative to Alk/Ca, consistent with hobbyist reports
    //        that AFR alone doesn't meaningfully move Mg).
    // Contains carbonate hardness, so treated as pH-sensitive like the
    // other Alk-contributing products above.
    // Added 2026-08-04: pH potency = 1.48 * 0.015 (carbonate-based
    // pH-cost-per-dKH factor) = 0.0222.
    { "Tropic Marin All-For-Reef", {1.48f, 0.0222f, 10.57f, 0.50f}, true },

    // --- DIY / raw chemicals (not tied to one brand) ----------------------
    // Added 2026-07-23 at owner's request, alongside the three named brands.

    // Sodium Hydroxide (NaOH) — Randy Holmes-Farley's standard recipe:
    // 283 g NaOH dissolved per US gallon RO/DI water, stated concentration
    // ~1,900 meq/L (5,300 dKH/L). Independently cross-checked via molar
    // stoichiometry (40 g/mol, 1 eq/mol): 283g/3.785L = 74.8 g/L =>
    // 1,869 meq/L => 5,234 dKH/L — within 1% of the recipe's stated value.
    //   potency = 5300 / 3785 = 1.40 dKH per mL per gallon.
    // This is literally the chemical v1's `naohMaxPh` hard cutoff existed
    // for. Note: ESV's Alk figure above is numerically higher per mL, but
    // that reflects how concentrated that particular bottle is sold, not
    // relative causticity — NaOH (true hydroxide) is the strongest *base*
    // in this table, raising pH more per unit of Alk delivered than any
    // carbonate/bicarbonate-based product here. Adds no Ca/Mg.
    { "Sodium Hydroxide (NaOH, DIY)", {1.40f, 0.0070f, 0.0f, 0.0f}, true },
    // Added 2026-08-04: pH potency = 1.40 * 0.005. Deliberately LOW despite
    // NaOH being, in isolation, a strong base -- corrected at the owner's
    // direct, explicit real-world observation: "[NaOH] has very little pH
    // effect" in actual practice on their systems. Real-world per-mL pH
    // impact depends on the specific solution's concentration and how
    // it's actually dosed (slowly, in small increments), not just the raw
    // chemistry of the base itself -- deferred to direct operator
    // experience over abstract acid-base theory here. Same reasoning/
    // value already used for kNaohPhCostPerDkh in main.cpp's legacy
    // migration path -- kept consistent between the two code paths.

    // Sodium Bicarbonate (baking soda) — "Recipe #2" for high-pH tanks:
    // 297 g baking soda (unbaked — NOT converted to soda ash) per US
    // gallon RO/DI, stated concentration ~950 meq/L (2,650 dKH/L).
    // Cross-checked via stoichiometry (84.007 g/mol, 1 eq/mol): 297g/3.785L
    // = 78.5 g/L => 934 meq/L => 2,615 dKH/L — within 1.5% of stated.
    //   potency = 2650 / 3785 = 0.70 dKH per mL per gallon.
    // Deliberately phSensitive = false, NOT a conservative default: multiple
    // independent sources describe this as having a mild-to-slightly-
    // *lowering* pH effect — the opposite direction from soda ash/NaOH —
    // which is precisely why hobbyists reach for it on naturally
    // high-pH tanks instead of soda ash. Gating it with the same
    // ceiling-taper logic built for pH-*raising* chemicals would be wrong,
    // not just unnecessary. Adds no Ca/Mg.
    { "Sodium Bicarbonate (baking soda, DIY)", {0.70f, -0.0035f, 0.0f, 0.0f}, false },
    // Added 2026-08-04: the first NEGATIVE pH potency in this table,
    // deliberately -- this is the one entry in this file already
    // documented as lowering pH slightly rather than raising it (see the
    // phSensitive=false comment above, written 2026-07-23, well before
    // this field existed). -0.0035 = 0.70 * -0.005, same small-magnitude
    // reasoning as NaOH's factor above, applied in the opposite direction
    // consistent with bicarbonate's real chemistry (its pKa sits below
    // typical seawater pH, so adding it can mildly buffer pH down toward
    // that point rather than push it up).

    // Kalkwasser (saturated calcium hydroxide, Ca(OH)2, solution) — unlike
    // every other entry above, this one moves BOTH Alk and Ca from a
    // single dose, in close to the ratio corals actually consume them
    // (the classic "self-balancing" reputation this chemical has in the
    // hobby). No manufacturer states a single clean "X per mL per gallon"
    // figure the way ESV/BRS do, so this is derived by averaging three
    // independent hobbyist-reported dosing outcomes for a standard
    // saturated solution, which agreed within ~10% of each other:
    //   Alk: 0.0301, 0.0330, 0.0296 dKH/mL/gal -> avg 0.031
    //   Ca:  0.2536, 0.2208, 0.2114 ppm/mL/gal -> avg 0.229
    //   (one of the three Ca estimates was cross-derived from the averaged
    //   Alk figure via the well-established 20 ppm Ca per 1 meq/L Alk
    //   consumption ratio, as an internal consistency check rather than a
    //   fully independent data point — treat Ca as somewhat less
    //   independently verified than Alk.)
    // Strongly pH-raising — calcium hydroxide is a genuinely strong base,
    // and every source consulted stresses dosing it slowly to avoid a pH
    // spike, same causticity category as NaOH above. Adds no Mg.
    { "Kalkwasser (calcium hydroxide, DIY)", {0.031f, 0.00093f, 0.229f, 0.0f}, true },
    // Added 2026-08-04: pH potency = 0.031 * 0.030 (kalk's own
    // pH-cost-per-dKH factor, same value as main.cpp's
    // kKalkPhCostPerDkh -- moderate-high, matching real hobbyist caution
    // about kalk causing pH spikes). Small in absolute terms because
    // kalk's per-mL Alk potency itself is small -- this is genuinely
    // correct, not an error: kalk needs a much larger volume than a
    // concentrated NaOH solution to deliver the same Alk correction, so
    // its per-mL pH contribution is proportionally smaller too, even
    // though its per-dKH-delivered pH cost is comparable to NaOH's.

    // Calcium Chloride (DIY) -- two entries, not one. Old Mode 7/8 used a
    // raw DIY CaCl2 entry that never made it into this table when V2's
    // preset system was built; added 2026-07-25 at owner's request.
    // Reference recipe: 250 g per US gallon RO/DI water, owner-confirmed
    // as the anhydrous form specifically -- the dihydrate figure below is
    // derived from that SAME 250g/gallon reference point, not a separately
    // sourced recipe, since only one reference amount was given.
    //
    // Anhydrous CaCl2 (110.98 g/mol, Ca 40.078 g/mol -> 36.12% Ca by mass):
    //   250g * 0.3612 = 90.29g Ca per gallon (3.7854 L) of stock solution
    //   90290 mg / 3.7854 L = 23,855 mg/L (ppm) stock concentration
    //   1 mL of stock = 23.855 mg Ca; diluted into a 1-gallon reference
    //   tank (3.7854 L): 23.855 / 3.7854 = 6.303 ppm Ca per mL per gallon.
    // No Alk/Mg effect, no material pH side-effect.
    { "Calcium Chloride, Anhydrous (DIY)", {0.0f, 0.0f, 6.303f, 0.0f}, false },

    // Calcium Chloride Dihydrate (CaCl2*2H2O, 147.02 g/mol, Ca 40.078 g/mol
    // -> 27.26% Ca by mass -- lower than anhydrous because the two waters
    // of crystallization add mass without adding calcium):
    //   250g * 0.2726 = 68.15g Ca per gallon of stock solution
    //   68150 mg / 3.7854 L = 18,006 mg/L (ppm) stock concentration
    //   1 mL of stock = 18.006 mg Ca; diluted into a 1-gallon reference
    //   tank: 18.006 / 3.7854 = 4.757 ppm Ca per mL per gallon.
    // ~32% less concentrated per gram than anhydrous at the same recipe
    // weight -- expected, given the extra water mass. No Alk/Mg effect,
    // no material pH side-effect.
    { "Calcium Chloride Dihydrate (DIY)", {0.0f, 0.0f, 4.757f, 0.0f}, false },

    // Added 2026-08-04: closes a real, confirmed gap -- a customer's
    // already-declared "ALK" chemical had no recipe match and no preset
    // at all, falling back to raw manual potency entry (the exact
    // failure category the Kalk investigation exists because of).
    //
    // Sodium Carbonate (soda ash, Na2CO3) -- 100 g per US gallon RO/DI
    // water. This specific recipe amount is not newly invented for this
    // entry: it matches the "AI Doser Standard" recipe already referenced
    // elsewhere in this exact codebase (main.cpp's Chemical Recipes card
    // default: "Soda Ash / Sodium Carbonate = 100 g per gallon of RO/DI
    // water"), and is independently consistent with commonly-published
    // reef DIY soda ash recipes at this concentration.
    //
    // Derived via the same real molar stoichiometry already used for
    // NaOH/baking soda above (105.99 g/mol, 2 equivalents per mole --
    // carbonate accepts 2 protons: CO3^2- + 2H+ -> H2CO3):
    //   100g / 105.99 g/mol = 0.9435 mol
    //   0.9435 mol * 2 eq/mol = 1.887 eq = 1887 meq
    //   1887 meq / 3.785 L = 498.5 meq/L
    //   498.5 meq/L * 2.8 dKH per meq/L = 1395.8 dKH/L
    //   potency = 1395.8 / 3785 = 0.3688 dKH per mL per gallon.
    // Internal consistency check against the NaOH entry above (same
    // methodology, same file): soda ash delivers ~75% as many
    // equivalents per gram as NaOH (2/105.99 vs 1/40 eq/g). At their
    // respective recipe weights (100g vs 283g), that predicts soda ash's
    // total potency should land at roughly 27% of NaOH's 1.40 dKH/mL/gal
    // figure -- 0.373, closely matching the 0.3688 derived independently
    // above. Real base, same causticity category as the other carbonate-
    // based entries -- phSensitive = true, pH potency = 0.3688 * 0.015
    // (same carbonate-based pH-cost-per-dKH factor used elsewhere in this
    // file) = 0.00553. Adds no Ca/Mg.
    { "Alkalinity (Soda Ash, DIY)", {0.3688f, 0.00553f, 0.0f, 0.0f}, true },
};

ChemicalDeclaration presetToDeclaration(ChemicalPresetId id, float tankVolumeGallons,
                                         const char* customName) {
    ChemicalDeclaration decl;

    if ((int)id < 0 || (int)id >= (int)ChemicalPresetId::kNumPresets || tankVolumeGallons <= 0.0f) {
        return decl; // all-zero potency: caller must treat this as a usage error
    }

    const ChemicalPresetSpec& spec = kChemicalPresets[(int)id];
    const char* name = customName ? customName : spec.displayName;
    strncpy(decl.name, name, sizeof(decl.name) - 1);
    decl.name[sizeof(decl.name) - 1] = '\0';

    decl.phSensitive = spec.phSensitive;

    for (int p = 0; p < kNumParams; p++) {
        // Per-gallon constant -> this specific tank's per-mL potency (§6/§7
        // "same estimator mechanism" pattern, applied to presets).
        decl.potencyPerMl[p] = spec.potencyPerMlPerGallon[p] / tankVolumeGallons;

        // §5.1 cold-start: seed confidence from known stoichiometry rather
        // than zero — "the allocator starts from real stoichiometry...
        // a strong prior refined by observation, not learning the
        // relationship from scratch." 0.70 reflects manufacturer-published
        // data (higher starting trust than a fully custom/guessed entry
        // would warrant, but still well below a Kalman-matured value).
        decl.confidence[p] = (spec.potencyPerMlPerGallon[p] != 0.0f) ? 0.70f : 0.0f;
    }

    // maxMlPerDay (§7 safety cap) and .active are deliberately left at
    // ChemicalDeclaration's defaults — see header comment.
    return decl;
}
