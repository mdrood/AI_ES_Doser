#pragma once

constexpr char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AIDoser | Local Dashboard</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@500;600;700&family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@500;600;700&display=swap" rel="stylesheet">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <!-- Firebase SDKs are only used when this dashboard is served from HTTPS Firebase Hosting. -->
  <script src="https://www.gstatic.com/firebasejs/10.12.2/firebase-app-compat.js"></script>
  <script src="https://www.gstatic.com/firebasejs/10.12.2/firebase-database-compat.js"></script>
  <script src="https://www.gstatic.com/firebasejs/10.12.2/firebase-messaging-compat.js"></script>
  <style>
    :root{
      /* Reef instrument panel palette -- deep lagoon base, coral as the
         one signature accent (echoing actual coral color rather than the
         generic cyan/blue every AI dashboard defaults to), kelp as a
         secondary "alive/active" tone. */
      --bg:#04141a;
      --bg-soft:#071f27;
      --card:rgba(9,36,45,.88);
      --card-2:rgba(21,58,66,.72);
      --border:rgba(127,166,160,.20);
      --text:#eaf6f1;
      --muted:#7fa6a0;
      --accent:#ff6b4a;
      --accent-2:#ff8b6e;
      --kelp:#2dd9b5;
      --success:#4fe0a8;
      --danger:#ff5470;
      --warning:#ffb648;
      --shadow:0 16px 50px rgba(2,10,12,.45);
      --font-display:'Space Grotesk',system-ui,sans-serif;
      --font-body:'Inter',system-ui,-apple-system,sans-serif;
      --font-mono:'JetBrains Mono',ui-monospace,monospace;
    }
    *{box-sizing:border-box}
    body{
      margin:0;
      min-height:100vh;
      font-family:var(--font-body);
      color:var(--text);
      background:
        radial-gradient(circle at top left, rgba(45,217,181,.38), transparent 34%),
        radial-gradient(circle at top right, rgba(255,107,74,.18), transparent 26%),
        linear-gradient(180deg,#03110f 0%,#04141a 100%);
      background-size:200% 200%,200% 200%,100% 100%;
      background-attachment:fixed;
      animation:causticDrift 26s ease-in-out infinite;
      padding:18px;
    }
    @keyframes causticDrift{
      0%,100%{background-position:0% 0%,100% 0%,0 0}
      50%{background-position:6% 4%,94% 5%,0 0}
    }
    @media (prefers-reduced-motion:reduce){
      body{animation:none}
    }
    .shell{width:100%;max-width:1220px;margin:0 auto}
    .hero{
      display:flex;justify-content:space-between;align-items:center;gap:18px;
      padding:24px;border:1px solid var(--border);border-radius:24px;
      background:linear-gradient(180deg, rgba(9,36,45,.95), rgba(9,36,45,.78));
      box-shadow:var(--shadow);backdrop-filter:blur(14px);margin-bottom:18px;
    }
    .hero-left{display:flex;gap:16px;align-items:center}
    .logo{
      width:58px;height:58px;border-radius:18px;
      background:linear-gradient(135deg,#ff6b4a,#2dd9b5);
      box-shadow:0 12px 30px rgba(255,107,74,.25);
      position:relative;overflow:hidden;
    }
    .logo:before,.logo:after{
      content:"";position:absolute;border:2px solid rgba(255,255,255,.9);border-radius:999px;
    }
    .logo:before{width:34px;height:34px;left:10px;top:11px;border-top-color:transparent;border-left-color:transparent;transform:rotate(28deg)}
    .logo:after{width:18px;height:18px;right:8px;bottom:8px;border-top-color:transparent;border-right-color:transparent;opacity:.8}
    h1{margin:0;font-size:1.85rem;letter-spacing:-.01em;font-family:var(--font-display);font-weight:700}
    .sub{color:var(--muted);margin-top:4px;font-size:.95rem}
    .hero-right{display:flex;gap:10px;flex-wrap:wrap;justify-content:flex-end}
    .chip{
      display:inline-flex;align-items:center;gap:8px;
      padding:10px 14px;border-radius:999px;border:1px solid var(--border);
      background:rgba(2,10,12,.38);color:var(--muted);font-weight:700;font-size:.82rem
    }
    .dot{width:8px;height:8px;border-radius:50%;background:var(--accent);box-shadow:0 0 10px rgba(255,107,74,.65)}
    .chips{
      display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:12px;margin-bottom:18px;
    }
    .sensor{
      border:1px solid var(--border);border-radius:18px;padding:16px;
      background:linear-gradient(180deg, rgba(9,36,45,.95), rgba(9,36,45,.72));
      box-shadow:var(--shadow);
    }
    .sensor .label{color:var(--muted);font-size:.76rem;text-transform:uppercase;letter-spacing:.14em;font-weight:600}
    .sensor .value{font-family:var(--font-mono);font-size:1.5rem;font-weight:600;margin-top:8px;color:#f4fbf8;font-variant-numeric:tabular-nums;letter-spacing:-.01em}
    .sensor .unit{font-size:.92rem;color:var(--accent);margin-left:4px}
    /* Masonry via CSS columns, not CSS Grid: with Grid, a short card
       sharing a row with a tall neighbor still gets a row TRACK sized to
       the tallest sibling, leaving visible dead space below the short
       card even with align-items:start -- that's the real mechanism
       behind "big empty spaces" between cards of different lengths.
       Columns let each card flow independently, closing those gaps. */
    .grid{column-gap:18px;column-count:1}
    @media (min-width:821px){.grid{column-count:2}}
    @media (min-width:1201px){.grid{column-count:3}}
    .grid .card{break-inside:avoid;width:100%;display:inline-block;margin:0 0 18px}
    .card{
      border:1px solid var(--border);border-radius:20px;padding:20px;
      background:linear-gradient(180deg, rgba(9,36,45,.92), rgba(9,36,45,.76));
      box-shadow:var(--shadow);backdrop-filter:blur(12px);
    }
    .card-title{
      display:flex;justify-content:space-between;align-items:center;gap:12px;
      margin-bottom:16px
    }
    .card-title h3{
      margin:0;color:var(--accent);font-size:.86rem;text-transform:uppercase;letter-spacing:.16em;font-family:var(--font-display);font-weight:600
    }
    .meta{font-size:.78rem;color:var(--muted)}
    .btn-row,.three{display:grid;gap:10px}
    .btn-row{grid-template-columns:repeat(3,1fr)}
    .two{display:grid;grid-template-columns:1fr 1fr;gap:12px}
    .three{grid-template-columns:repeat(3,1fr)}
    input,select,button{
      width:100%;border-radius:12px;font:inherit
    }
    input,select{
      border:1px solid var(--border);
      background:rgba(2,10,12,.45);color:var(--text);
      padding:12px 13px;outline:none
    }
    input:focus,select:focus{border-color:rgba(255,107,74,.55);box-shadow:0 0 0 3px rgba(255,107,74,.12)}
    button{
      border:none;padding:12px 14px;font-weight:800;cursor:pointer;transition:.18s ease;
      background:linear-gradient(135deg,var(--accent),var(--accent-2));color:#04141a;
    }
    button:hover{transform:translateY(-1px);filter:brightness(1.04)}
    button.sec{
      background:transparent;color:var(--accent);border:1px solid rgba(255,107,74,.34)
    }
    button.soft{
      background:rgba(21,58,66,.9);color:var(--text);border:1px solid var(--border)
    }
    button.danger{
      background:linear-gradient(135deg,#ff5470,#c81e46);color:white
    }
    .mode-btn.active,
    .pill-btn.active{
      background:linear-gradient(135deg, rgba(255,107,74,.25), rgba(255,139,110,.18));
      color:var(--accent);border:1px solid rgba(255,107,74,.45)
    }
    .mode-btn,.pill-btn{
      background:rgba(21,58,66,.85);color:var(--muted);border:1px solid var(--border)
    }
    .stack{display:flex;flex-direction:column;gap:12px}
    .line{display:flex;justify-content:space-between;gap:10px;align-items:center}
    .line .k{color:var(--muted);font-size:.84rem}
    .line .v{font-weight:800}
    .help{font-size:.78rem;color:var(--muted);line-height:1.5}
    .status-pill{
      display:inline-flex;align-items:center;gap:8px;padding:8px 12px;
      border-radius:999px;border:1px solid var(--border);background:rgba(2,10,12,.35);
      font-size:.8rem;font-weight:700;color:var(--muted)
    }
    .cal-list{display:flex;flex-direction:column;gap:12px}
    .cal-item{
      padding:16px 0;border-bottom:1px solid rgba(127,166,160,.14)
    }
    .cal-item:last-child{border-bottom:none;padding-bottom:2px}
    .cal-item:first-child{padding-top:2px}
    .cal-top{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:10px}
    .cal-name{font-weight:900}
    .cal-sub{font-size:.76rem;color:var(--muted);margin-top:2px}
    .flow-badge{
      padding:8px 10px;border-radius:12px;background:rgba(255,107,74,.10);
      border:1px solid rgba(255,107,74,.22);font-size:.82rem;font-weight:600;color:var(--accent);font-family:var(--font-mono);font-variant-numeric:tabular-nums
    }

    .plan-list{display:flex;flex-direction:column;gap:10px}
    .plan-row{
      display:flex;align-items:center;justify-content:space-between;gap:12px;
      padding:13px 2px;border-bottom:1px solid rgba(127,166,160,.14)
    }
    .plan-row:last-child{border-bottom:none}
    .plan-left{display:flex;align-items:center;gap:10px;min-width:0}
    .plan-name{font-weight:900;color:#f4fbf8}
    .plan-sub{font-size:.75rem;color:var(--muted);margin-top:2px}
    .plan-amt{font-weight:600;color:var(--accent);font-size:1.1rem;white-space:nowrap;font-family:var(--font-mono);font-variant-numeric:tabular-nums}
    .chart-stack{display:grid;grid-template-columns:1fr;gap:26px}
    .footer-note{margin-top:14px;font-size:.75rem;color:var(--muted)}
    .chem-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}
    .chem-item{padding:16px 0;border-bottom:1px solid rgba(127,166,160,.14)}
    .chem-item:last-child{border-bottom:none;padding-bottom:2px}
    .chem-item:first-child{padding-top:2px}
    .chem-top{display:flex;justify-content:space-between;gap:10px;align-items:flex-start;margin-bottom:10px}
    .chem-name{font-weight:900}
    .chem-left{font-size:.85rem;color:var(--muted);margin-top:4px}
    .chem-badge{padding:7px 9px;border-radius:999px;border:1px solid var(--border);font-size:.75rem;font-weight:900;color:var(--muted)}
    .chem-badge.warn{color:var(--warning);border-color:rgba(255,182,72,.45);background:rgba(255,182,72,.08)}
    .chem-badge.severe{color:var(--danger);border-color:rgba(255,84,112,.5);background:rgba(255,84,112,.10)}

    .safety-table{display:grid;grid-template-columns:1.25fr repeat(3,1fr);gap:10px;align-items:end;margin-top:12px}
    .safety-head{color:var(--muted);font-size:.72rem;text-transform:uppercase;letter-spacing:.10em;font-weight:900}
    .safety-pump{font-weight:900;color:#f4fbf8;padding:12px 0}
    .safety-used{font-size:.72rem;color:var(--muted);margin-top:4px}
    @media (max-width: 760px){.safety-table{grid-template-columns:1fr}.safety-head{display:none}.safety-pump{padding-top:8px}}
    .chem-level-bar{margin:12px 0 10px;border-radius:999px;height:18px;overflow:hidden;background:rgba(9,36,45,.82);border:1px solid rgba(127,166,160,.24)}
    .chem-level-fill{height:100%;border-radius:999px;background:linear-gradient(90deg,var(--accent),var(--success));width:0%;transition:width .35s ease}
    .chem-level-fill.warn{background:linear-gradient(90deg,var(--warning),#e8933a)}
    .chem-level-fill.severe{background:linear-gradient(90deg,var(--danger),#c81e46)}
    .chem-level-meta{display:flex;justify-content:space-between;gap:10px;align-items:center;font-size:.78rem;color:var(--muted);font-weight:800;margin-bottom:10px}
    .chem-level-percent{font-size:1.1rem;color:#f4fbf8;font-weight:700;font-family:var(--font-mono);font-variant-numeric:tabular-nums}

    .history-toolbar{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-bottom:14px}
    .history-toolbar button{width:auto;min-width:130px}
    .history-select{width:auto;min-width:150px}
    .history-summary{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin:12px 0 16px}
    .history-stat{border:1px solid rgba(127,166,160,.16);background:rgba(2,10,12,.28);border-radius:14px;padding:12px}
    .history-stat .k{color:var(--muted);font-size:.72rem;text-transform:uppercase;letter-spacing:.10em}
    .history-stat .v{font-size:1.2rem;font-weight:700;color:#f4fbf8;margin-top:5px;font-family:var(--font-mono);font-variant-numeric:tabular-nums}
    .danger-warning{
      border:2px solid rgba(255,84,112,.78);
      background:linear-gradient(180deg, rgba(92,12,28,.72), rgba(48,6,16,.62));
      color:#ffd9de;
      border-radius:16px;
      padding:14px;
      margin:12px 0;
      font-weight:900;
      box-shadow:0 0 28px rgba(255,84,112,.18);
    }
    .danger-warning small{display:block;margin-top:6px;color:#ffc2ca;font-weight:700;line-height:1.45}
    .recipe-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:14px;margin-top:14px}
    .recipe-card{border:1px solid rgba(127,166,160,.20);background:rgba(2,10,12,.34);border-radius:18px;padding:16px}
    .recipe-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;margin-bottom:12px}
    .recipe-title{font-size:1.02rem;font-weight:950;color:#f4fbf8}
    .recipe-pump{font-size:.72rem;font-weight:900;color:var(--accent);border:1px solid rgba(255,107,74,.32);background:rgba(255,107,74,.08);border-radius:999px;padding:6px 9px;white-space:nowrap}
    .recipe-row{display:grid;grid-template-columns:1fr;gap:8px;margin-top:10px}
    .recipe-label{font-size:.74rem;text-transform:uppercase;letter-spacing:.10em;color:var(--muted);font-weight:850}
    .recipe-note{font-size:.76rem;color:var(--muted);line-height:1.45;margin-top:8px}
    .recipe-result{margin-top:12px;border-radius:14px;padding:12px;background:rgba(255,107,74,.08);border:1px solid rgba(255,107,74,.20)}
    .recipe-result .k{font-size:.72rem;color:var(--muted);text-transform:uppercase;letter-spacing:.10em;font-weight:850}
    .recipe-result .v{font-size:1.12rem;color:var(--accent);font-weight:950;margin-top:4px}
    .recipe-result .u{font-size:.78rem;color:var(--muted);font-weight:800;margin-left:4px}
    .recipe-red-note{
      margin-top:10px;
      padding:10px 12px;
      border-radius:12px;
      border:1px solid rgba(255,84,112,.42);
      background:rgba(92,12,28,.30);
      color:#ffc2ca;
      font-size:.78rem;
      font-weight:850;
      line-height:1.45;
    }
    .recipe-red-note b{color:#ffd9de}
    .recipe-source-pill{
      display:inline-flex;
      margin-top:8px;
      padding:7px 10px;
      border-radius:999px;
      border:1px solid rgba(255,182,72,.44);
      background:rgba(255,182,72,.10);
      color:#ffe1a8;
      font-size:.74rem;
      font-weight:950;
    }
    .recipe-full{grid-column:1 / -1}
    @media (max-width: 820px){
      .hero{flex-direction:column;align-items:flex-start}
      .hero-right{justify-content:flex-start}
    }
    @media (max-width: 640px){
      .btn-row,.two,.three{grid-template-columns:1fr}
      body{padding:12px}
      .hero{padding:18px}
      .sensor .value{font-size:1.35rem}
    }
  </style>
</head>
<body>
  <!-- §8/§8.5 One-time Setup Wizard. Shows only on a genuinely fresh device
       (see shouldShowSetupWizard()). Handles everything V2 needs before the
       device starts dosing a real tank: declaring chemicals (replaces the
       old fixed Mode 1-8 picker), pump calibration, tank/coral load,
       chemistry targets, and the required baseline test. Every step calls
       the same backend endpoints the rest of the dashboard uses -- no
       parallel config path. Wi-Fi provisioning and initial device-online
       confirmation happen on the separate cloud device-setup.html page,
       BEFORE this ever loads; this page assumes the device is already
       online and reachable. -->
  <div id="setupWizardOverlay" style="display:none;position:fixed;inset:0;z-index:1000;background:linear-gradient(180deg,#03110f 0%,#04141a 100%);overflow-y:auto;padding:24px">
    <div style="max-width:640px;margin:0 auto">
      <div style="text-align:center;margin-bottom:24px">
        <div class="logo" style="margin:0 auto 16px"></div>
        <h1 style="font-size:1.5rem">Welcome to AIDoser</h1>
        <div class="meta" id="wizardStepIndicator">Step 1 of 7</div>
        <div class="chem-level-bar" style="margin-top:10px;max-width:280px;margin-left:auto;margin-right:auto">
          <div class="chem-level-fill" id="wizardProgressFill" style="width:14.3%"></div>
        </div>
      </div>

      <div class="card" id="wizardStep1">
        <div class="card-title"><h3>Let's Get Your Tank Set Up</h3></div>
        <p class="help">This will only take a few minutes. We'll walk through your tank, what you're dosing, pump calibration, chemistry targets, and one required water test -- everything the AI needs to dose safely and correctly for <em>your</em> tank, not a generic guess.</p>
        <p class="help">Already fully configured on this device? You can skip straight to the full dashboard.</p>
        <div class="btn-row" style="grid-template-columns:1fr 1fr;margin-top:16px">
          <button onclick="wizardGoTo(2)">Get Started</button>
          <button class="sec" onclick="wizardSkipToDashboard()">Skip, I'm Already Set Up</button>
        </div>
      </div>

      <div class="card" id="wizardStep2" style="display:none">
        <div class="card-title"><h3>About Your Tank</h3><span class="status-pill">Step 2 of 7</span></div>
        <div class="line" style="margin-top:4px"><div class="k">Tank Volume (Gallons)</div><input type="number" id="wizardTankGal" step="0.1" placeholder="e.g. 120"></div>
        <div class="line" style="margin-top:8px"><div class="k">Coral Load</div>
          <select id="wizardCoralLoad">
            <option value="light">Light (mostly fish, few corals)</option>
            <option value="moderate" selected>Moderate (mixed reef)</option>
            <option value="heavy">Heavy (dense LPS/softies)</option>
            <option value="sps">SPS-Dominant</option>
          </select>
        </div>
        <p class="help" style="margin-top:8px">Not sure on coral load? "Moderate" is a safe default -- the AI refines this from real measurements over time either way.</p>
        <div class="btn-row" style="grid-template-columns:1fr 1fr;margin-top:16px">
          <button onclick="wizardSaveTankInfo()">Save &amp; Continue</button>
          <button class="sec" onclick="wizardGoTo(1)">Back</button>
        </div>
        <div class="help" id="wizardStep2Error" style="color:var(--danger);margin-top:8px"></div>
      </div>

      <div class="card" id="wizardStep3" style="display:none">
        <div class="card-title"><h3>What Are You Dosing?</h3><span class="status-pill" id="wizardChemCountPill">0 declared</span></div>
        <p class="help">For each physical pump, tell us what's connected to it -- a known product (potency is already known, no math needed) or something you mix yourself.</p>
        <p class="help" id="wizardClearAllNote" style="display:none">Already showing chemicals you didn't add? A leftover default from before this wizard ran -- clear them and start fresh below.</p>
        <div id="wizardChemList" style="margin-top:12px"></div>
        <button class="sec" id="wizardClearAllBtn" style="display:none;margin-top:8px" onclick="wizardClearAllChemicals()">Clear All &amp; Start Fresh</button>
        <button class="sec" style="margin-top:12px" onclick="wizardShowAddChemicalForm()">+ Add Chemical</button>

        <div id="wizardAddChemicalForm" style="display:none;margin-top:14px;padding-top:14px;border-top:1px solid var(--border)">
          <div class="line"><div class="k">Name</div><input type="text" id="wizardNewChemName" placeholder="e.g. Alk Dose" style="width:160px"></div>
          <div class="line" style="margin-top:8px"><div class="k">Product</div>
            <select id="wizardNewChemPreset" onchange="wizardOnNewChemPresetChange()" style="width:240px">
              <option value="-1">Something else / I know my potency</option>
            </select>
          </div>

          <!-- DIY recipe override: only shown for NaOH/Baking Soda, where
               concentration genuinely depends on how the customer mixes it
               (exactly the "old Mode 7/8 needed customer input on strength
               and formula" case). Commercial presets (BRS/ESV/Tropic Marin)
               and saturated Kalkwasser need none of this -- potency is
               fixed and known. -->
          <div id="wizardRecipeFields" style="display:none;margin-top:8px">
            <div class="line"><div class="k">Recipe you mix (grams per gallon RO/DI)</div><input type="number" step="1" id="wizardRecipeGrams" style="width:100px"></div>
            <p class="help" id="wizardRecipeHint" style="margin-top:4px"></p>
          </div>

          <div id="wizardNewChemCustomFields" style="display:none;margin-top:8px">
            <div class="three">
              <input type="number" step="0.0001" id="wizardNewChemAlk" placeholder="dKH per mL">
              <input type="number" step="0.0001" id="wizardNewChemCa" placeholder="ppm Ca per mL">
              <input type="number" step="0.0001" id="wizardNewChemMg" placeholder="ppm Mg per mL">
            </div>
            <div class="line" style="margin-top:8px"><div class="k">Raises pH (e.g. NaOH, kalkwasser)</div>
              <input type="checkbox" id="wizardNewChemPhSensitive">
            </div>
          </div>

          <div class="line" style="margin-top:8px"><div class="k">Physical Pump</div>
            <select id="wizardNewChemPump" style="width:120px"></select>
          </div>
          <div class="btn-row" style="margin-top:12px;grid-template-columns:1fr 1fr">
            <button onclick="wizardSubmitAddChemical()">Add</button>
            <button class="sec" onclick="wizardHideAddChemicalForm()">Cancel</button>
          </div>
          <div class="help" id="wizardAddChemicalError" style="color:var(--danger);margin-top:8px"></div>
        </div>

        <div class="btn-row" style="grid-template-columns:1fr 1fr;margin-top:16px">
          <button onclick="wizardGoTo(4)">Continue</button>
          <button class="sec" onclick="wizardGoTo(2)">Back</button>
        </div>
        <div class="help" id="wizardStep3Error" style="color:var(--danger);margin-top:8px"></div>
      </div>

      <div class="card" id="wizardStep4" style="display:none">
        <div class="card-title"><h3>Calibrate Your Pumps</h3><span class="status-pill">Step 4 of 7</span></div>
        <p class="help">Accurate calibration matters more than anything else here -- the AI's dosing math depends on knowing exactly how much each pump actually delivers. Run RO water only; don't connect concentrated chemicals yet.</p>
        <div id="wizardCalibrationList" style="margin-top:12px"></div>
        <div class="btn-row" style="grid-template-columns:1fr 1fr;margin-top:16px">
          <button onclick="wizardGoTo(5)">Continue</button>
          <button class="sec" onclick="wizardGoTo(3)">Back</button>
        </div>
      </div>

      <div class="card" id="wizardStep5" style="display:none">
        <div class="card-title"><h3>Chemistry Targets</h3><span class="status-pill">Step 5 of 7</span></div>
        <p class="help">The stable levels you want your tank to hold. The AI corrects toward these while respecting the safety caps auto-calculated from your tank size and coral load -- it won't try to reach a target instantly.</p>
        <div class="line" style="margin-top:12px"><div class="k">Alk Target (dKH)</div><input type="number" step="0.01" id="wizardTargetAlk" value="8.5"></div>
        <div class="line" style="margin-top:8px"><div class="k">Calcium Target (ppm)</div><input type="number" step="1" id="wizardTargetCa" value="450"></div>
        <div class="line" style="margin-top:8px" id="wizardTargetMgRow"><div class="k">Magnesium Target (ppm)</div><input type="number" step="1" id="wizardTargetMg" value="1440"></div>
        <p class="help" id="wizardNoMgNote" style="display:none;margin-top:4px">No declared chemical touches magnesium, so there's nothing to target here -- add one in Step 3 first if you want the AI to manage Mg.</p>
        <div class="line" style="margin-top:8px"><div class="k">pH Target</div><input type="number" step="0.01" id="wizardTargetPh" value="8.30" style="width:100px"></div>
        <p class="help" style="margin-top:4px">There's no pump that directly raises or lowers pH -- this works by favoring gentler Alk sources as pH nears this value, and holding back stronger ones (like NaOH) instead.</p>
        <button class="sec" style="margin-top:8px" onclick="wizardUseRecommendedTargets()">Use Recommended Defaults</button>
        <div class="btn-row" style="grid-template-columns:1fr 1fr;margin-top:16px">
          <button onclick="wizardSaveTargets()">Save &amp; Continue</button>
          <button class="sec" onclick="wizardGoTo(4)">Back</button>
        </div>
        <div class="help" id="wizardStep5Error" style="color:var(--danger);margin-top:8px"></div>
      </div>

      <div class="card" id="wizardStep6" style="display:none">
        <div class="card-title"><h3>Required: Baseline Water Test</h3><span class="status-pill">Step 6 of 7</span></div>
        <p class="help"><strong>The AI won't dose until this is entered</strong> -- it needs a real starting point, not a guess.</p>
        <div class="line" style="margin-top:12px"><div class="k">Alk (dKH)</div><input type="number" step="0.01" id="wizardAlk"></div>
        <div class="line" style="margin-top:8px"><div class="k">Calcium (ppm)</div><input type="number" step="1" id="wizardCa"></div>
        <div class="line" style="margin-top:8px" id="wizardBaselineMgRow"><div class="k">Magnesium (ppm)</div><input type="number" step="1" id="wizardMg"></div>
        <div class="line" style="margin-top:8px"><div class="k">pH</div><input type="number" step="0.01" id="wizardPh" placeholder="8.10" style="width:100px"></div>
        <div class="btn-row" style="grid-template-columns:1fr 1fr;margin-top:16px">
          <button id="wizardSaveTestBtn" onclick="wizardSaveBaselineTest()">Submit Test &amp; Continue</button>
          <button class="sec" onclick="wizardGoTo(5)">Back</button>
        </div>
        <div class="help" id="wizardStep6Error" style="color:var(--danger);margin-top:8px"></div>
        <p class="help" style="margin-top:12px">You won't need to test daily forever -- as the AI proves it can predict your tank, this stretches from <strong>daily</strong> to <strong>every other day</strong> to <strong>weekly</strong>. Track it anytime on the Manual Test Schedule card.</p>
      </div>

      <div class="card" id="wizardStep7" style="display:none">
        <div class="card-title"><h3>You're All Set</h3></div>
        <p class="help">Your AI Doser now has everything it needs: tank size, coral load, declared chemicals, calibrated pumps, chemistry targets, and a real baseline reading. It's starting in the Observation / Break-in Period -- dosing conservatively while it learns your tank's real behavior over the next several days.</p>
        <p class="help">You can re-run this wizard anytime if you overhaul your tank's setup.</p>
        <button style="margin-top:12px" onclick="wizardFinish()">Enter Dashboard</button>
      </div>
    </div>
  </div>

  <div class="shell">
    <section class="hero">
      <div class="hero-left">
        <div class="logo"></div>
        <div>
          <h1>AIDoser <span style="color:var(--accent)">Standalone</span></h1>
          <div class="sub">Beautiful local dashboard • internal flash is the local source of truth</div>
        </div>
      </div>
      <div class="hero-right">
        <div class="chip"><span class="dot"></span><span id="deviceChip">Device: Local</span></div>
        <div class="chip"><span class="dot" id="onlineDot"></span><span id="onlineText">Connected</span></div>
        <div class="chip"><span class="dot"></span><span id="syncBox">Connecting…</span></div>
      </div>
    </section>

    <section class="chips" id="sensorGrid">
      <div class="sensor"><div class="label">pH</div><div class="value" id="sensorPh">--</div></div>
      <div class="sensor"><div class="label">Temp</div><div class="value" id="sensorTemp">-- <span class="unit">°F</span></div></div>
      <div class="sensor"><div class="label">Alk</div><div class="value" id="sensorAlk">-- <span class="unit">dKH</span></div></div>
      <div class="sensor"><div class="label">Ca</div><div class="value" id="sensorCa">-- <span class="unit">ppm</span></div></div>
      <div class="sensor"><div class="label">Mg</div><div class="value" id="sensorMg">-- <span class="unit">ppm</span></div></div>
      <div class="sensor"><div class="label">PPT / SG</div><div class="value" id="sensorSal">--</div></div>
    </section>

    <section class="grid">
<div class="card">
        <div class="card-title">
          <h3>System State</h3>
          <span class="status-pill" id="opModeText">AUTO</span>
        </div>
        <div class="btn-row">
          <button class="mode-btn pill-btn" id="m0" onclick="setMode(0)">OFF</button>
          <button class="mode-btn pill-btn" id="m1" onclick="setMode(1)">AUTO</button>
          <button class="mode-btn pill-btn" id="m2" onclick="setMode(2)">MAN</button>
        </div>
        <button id="eStopBtn" class="danger" onclick="toggleEmergency()">🛑 Emergency Stop</button>
        <div class="footer-note">This is your local operating mode. It is separate from the 1–8 dosing implementation below.</div>
      </div>

<div class="card">
        <div class="card-title">
          <h3>Active Dosing Plan</h3>
          <span class="meta" id="planRealtimeMeta">Realtime from ESP32</span>
        </div>
        <div id="activePlanList" class="plan-list"></div>
        <div class="footer-note">This shows the current AI plan from <code>/api/status</code>. History graphs still use midnight daily records to keep Firebase/write cost down.</div>
      </div>

<div class="card">
        <div class="card-title">
          <h3>Manage Chemicals</h3>
          <span class="status-pill" id="chemCountPill">0 declared</span>
        </div>
        <div id="chemManageList"></div>
        <div class="footer-note" id="chemManageHelp">Each chemical is assigned to one physical pump (1-4). Removing a chemical is blocked if it would leave your current targets unreachable.</div>
        <button class="sec" style="margin-top:12px" onclick="showAddChemicalForm()">+ Add Chemical</button>
        <div id="addChemicalForm" style="display:none;margin-top:14px;padding-top:14px;border-top:1px solid var(--border)">
          <div class="line"><div class="k">Name</div><input type="text" id="newChemName" placeholder="e.g. Alk Dose" style="width:160px"></div>
          <div class="line" style="margin-top:8px"><div class="k">Product</div>
            <select id="newChemPreset" onchange="onNewChemPresetChange()" style="width:220px">
              <option value="-1">Custom (enter potency manually)</option>
            </select>
          </div>
          <div id="newChemCustomFields" style="display:none;margin-top:8px">
            <div class="three">
              <input type="number" step="0.0001" id="newChemAlk" placeholder="dKH per mL">
              <input type="number" step="0.0001" id="newChemCa" placeholder="ppm Ca per mL">
              <input type="number" step="0.0001" id="newChemMg" placeholder="ppm Mg per mL">
            </div>
            <div class="line" style="margin-top:8px"><div class="k">Raises pH (e.g. NaOH, kalkwasser)</div>
              <input type="checkbox" id="newChemPhSensitive">
            </div>
          </div>
          <div class="line" style="margin-top:8px"><div class="k">Physical Pump</div>
            <select id="newChemPump" style="width:120px"></select>
          </div>
          <div class="btn-row" style="margin-top:12px;grid-template-columns:1fr 1fr">
            <button onclick="submitAddChemical()">Add</button>
            <button class="sec" onclick="hideAddChemicalForm()">Cancel</button>
          </div>
          <div class="help" id="addChemicalError" style="color:var(--danger);margin-top:8px"></div>
        </div>
      </div>

<div class="card">
        <div class="card-title">
          <h3>Manual Water Test</h3>
          <span class="meta">Push values into AI immediately</span>
        </div>
        <div class="two">
          <input type="number" step="0.01" id="alk" placeholder="Alk dKH">
          <input type="number" step="0.1" id="ca" placeholder="Ca ppm">
          <input type="number" step="1" id="mg" placeholder="Mg ppm">
          <input type="number" step="0.01" id="ph" placeholder="pH">
        </div>
        <button id="saveTestBtn" onclick="saveTest()">Update AI Dosing Plan</button>
      </div>

<div class="card" id="manualTestPromptCard" style="display:none">
        <div class="card-title">
          <h3>Manual Test Schedule</h3>
          <span class="status-pill" id="manualTestPromptPill">--</span>
        </div>
        <div class="line"><div class="k">Last tested</div><div class="v" id="manualTestDaysAgo">--</div></div>
        <div class="line" style="margin-top:8px"><div class="k">Recommended interval</div><div class="v" id="manualTestInterval">--</div></div>
        <div style="margin-top:14px">
          <div class="chem-level-meta"><span>System maturity</span><span class="chem-level-percent" id="manualTestMaturityPct">--</span></div>
          <div class="chem-level-bar" title="How proven the AI's model of your tank is -- governs how often you need to test">
            <div class="chem-level-fill" id="manualTestMaturityFill" style="width:0%"></div>
          </div>
        </div>
        <div class="footer-note" id="manualTestPromptNote">As the AI proves it can predict your tank between tests, this interval stretches from daily toward weekly.</div>
      </div>

<!-- Added 2026-08-04, §4.2/§4.3: replaces the removed (confirmed dead
     for v2) legacy 7-Day Alk/Calcium Demand Learning cards -- this one
     is genuinely working, reads real data from the engine's own
     history buffer, not a v1 leftover. -->
<div class="card" id="weekMonthTrendCard">
  <div class="card-title">
    <h3>Week / Month Trend Learning</h3>
    <span class="meta">Real logged measurements, not extrapolation</span>
  </div>
  <div class="help">Once enough real tests exist, this shows this tank's actual short-term (7-day) and longer-term (30-day) trend for each parameter -- and flags it if the two disagree enough to suggest real demand is drifting, not just noise.</div>
  <div id="weekMonthTrendRows"></div>
</div>

<div class="card">
        <div class="card-title">
          <h3>Apex Controller</h3>
          <span class="status-pill" id="apexStatePill">Disabled</span>
        </div>
        <div class="two">
          <div>
            <div class="help" style="margin-bottom:8px;">Enable Apex polling</div>
            <select id="apEn">
              <option value="0">Disabled</option>
              <option value="1">Enabled</option>
            </select>
          </div>
          <div>
            <div class="help" style="margin-bottom:8px;">Apex IP</div>
            <input type="text" id="apIp" placeholder="192.168.1.50">
          </div>
        </div>
        <button class="sec" onclick="saveApex()">Save Apex Config</button>
      </div>

<div class="card" style="grid-column:1 / -1">
        <div class="card-title">
          <h3>Chemical Reservoirs</h3>
          <span class="meta">Local ESP32 memory only • no Firebase volume storage</span>
        </div>
        <div class="chem-grid" id="chemicalLevelGrid"></div>
        <div class="three" style="margin-top:12px">
          <button class="soft" onclick="saveChemicalCapacity()">Save Capacity</button>
          <button class="sec" onclick="setChemicalCurrentLevel()">Set Current Level</button>
          <button class="sec" onclick="fillChemicalToFull()">Fill to Full</button>
        </div>
        <div class="footer-note">Each bar shows the estimated chemical left in that reservoir. Capacity is the container size. Current Level is what is actually in the container now, useful for partial refills like setting a 5 gal bucket to 3.0 gal. Firmware subtracts only confirmed pump doses. Warning at 1 gallon left, severe at 0.5 gallon left.</div>
      </div>

<div class="card">
        <div class="card-title">
          <h3>Quick Dose</h3>
          <span class="meta">Runs the selected physical pump now</span>
        </div>
        <div class="two">
          <select id="pSel"></select>
          <input type="number" step="0.1" id="v_ml" placeholder="Dose amount (mL)">
        </div>
        <button onclick="liveDose()">Run Live Dose</button>
        <button class="sec" onclick="resetWiFi()" style="margin-top:10px">Reset WiFi / Provisioning</button>
      </div>

<div class="card" style="grid-column:1 / -1">
        <div class="card-title">
          <h3>Flow Calibration</h3>
          <span class="meta">Only pumps used by the selected dosing implementation are shown</span>
        </div>
        <div class="cal-list" id="calibrationContainer"></div>
        <div class="footer-note">These fields save each pump’s flow in mL/min locally. That keeps the dashboard and ESP32 aligned without removing your working dose logic.</div>
      </div>

<div class="card">
  <div class="card-title"><h3>System Geometry</h3></div>
  <div class="line">
    <div class="k">Tank Volume (Gallons)</div>
    <input type="number" id="tankGal" step="0.1" placeholder="e.g. 120" style="width:100px" oninput="updateRecipePreview()">
  </div>
  <button class="sec" onclick="saveVol()">Update Volume</button>
</div>

<div class="card">
  <h3>Lighting & Metabolism</h3>
  <label>Light Detection Source</label>
  <select id="lightSrc" onchange="markLightDirty(); uiToggleLights()">
    <option value="0">Internal Timer</option>
    <option value="1">Apex Outlet Polling</option>
  </select>

  <div id="lightTimerGroup">
    <div class="row">
      <div><label>Start</label><select id="lStart" onchange="markLightDirty()"></select></div>
      <div><label>End</label><select id="lEnd" onchange="markLightDirty()"></select></div>
    </div>
  </div>

  <div id="lightApexGroup" style="display:none;">
    <label>Apex Outlet Name</label>
    <input type="text" id="lOutlet" placeholder="e.g. BRS_Light_4_1" onfocus="markLightDirty()" oninput="markLightDirty()">
  </div>

  <button onclick="saveLightConfig()" class="btn-sec">Update AI Metabolism</button>
</div>

<div class="card">
        <div class="card-title">
          <h3>Firebase Notifications</h3>
          <span class="status-pill" id="notificationLevelPill">Warning+</span>
        </div>
        <select id="notificationLevelSelect" onchange="markNotificationDirty()">
          <option value="muted">Muted</option>
          <option value="severe">Severe only</option>
          <option value="warning">Severe + Warning</option>
          <option value="info">All: Severe + Warning + Info</option>
        </select>
        <button class="sec" onclick="saveNotificationLevel()">Save Notification Level</button>
        <button class="sec" onclick="enableFirebasePushNotifications()" style="margin-top:10px">Enable iPhone / Browser Push</button>
        <div class="help" id="pushNotificationStatus" style="margin-top:8px">Push setup status: not enabled from this browser.</div>
        <div class="help">This saves locally and lets firmware mirror <code>/devices/&lt;deviceId&gt;/settings/notificationLevel</code> to Firebase. Push notifications require opening the Firebase-hosted HTTPS dashboard and deploying <code>firebase-messaging-sw.js</code>.</div>
      </div>
    </section>
  </div>
  <div class="card" style="grid-column:1 / -1">
  <div class="card-title">
    <h3>Pump Dosing Safeties</h3>
    <span class="meta">Per-pump limits</span>
  </div>
  <div class="help">Each physical pump has its own accumulator threshold, maximum single dose, and maximum daily dose. This protects small chemical pumps without limiting large kalk dosing.</div>

  <div class="safety-table">
    <div class="safety-head">Pump</div>
    <div class="safety-head">Dose Threshold (mL)</div>
    <div class="safety-head">Max Single Dose (mL)</div>
    <div class="safety-head">Max Daily Dose (mL/day)</div>

    <div class="safety-pump"><span id="safetyPumpName0">P1</span><div class="safety-used" id="safetyUsed0">Used today: --</div></div>
    <input type="number" id="safeThr0" step="0.1" min="0.1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">
    <input type="number" id="safeMax0" step="1" min="1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">
    <input type="number" id="safeDay0" step="1" min="1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">

    <div class="safety-pump"><span id="safetyPumpName1">P2</span><div class="safety-used" id="safetyUsed1">Used today: --</div></div>
    <input type="number" id="safeThr1" step="0.1" min="0.1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">
    <input type="number" id="safeMax1" step="1" min="1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">
    <input type="number" id="safeDay1" step="1" min="1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">

    <div class="safety-pump"><span id="safetyPumpName2">P3</span><div class="safety-used" id="safetyUsed2">Used today: --</div></div>
    <input type="number" id="safeThr2" step="0.1" min="0.1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">
    <input type="number" id="safeMax2" step="1" min="1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">
    <input type="number" id="safeDay2" step="1" min="1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">

    <div class="safety-pump"><span id="safetyPumpName3">P4</span><div class="safety-used" id="safetyUsed3">Used today: --</div></div>
    <input type="number" id="safeThr3" step="0.1" min="0.1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">
    <input type="number" id="safeMax3" step="1" min="1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">
    <input type="number" id="safeDay3" step="1" min="1" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">
  </div>

  <button class="sec" onclick="saveDosingSafeties()" style="margin-top:14px">Update Pump Safety Rails</button>
  <div class="footer-note">Threshold: bucket amount required before a pump fires. Max Single Dose: cap for one pump run before the 60-second hardware cap. Max Daily Dose: calendar-day limit for each PHYSICAL PUMP, regardless of which chemical is assigned to it -- only used as the starting default when a chemical is first declared on that pump. To change an already-declared chemical's own daily cap, use "Daily Dose Cap (this chemical)" under Manage Chemicals instead -- that is what the dosing allocator actually enforces going forward.</div>
</div>

<!-- Removed 2026-08-02: six legacy v1 cards deleted outright (not just
     disabled) -- AI Chemistry Safeties, Mode 7 Day/Night Alk Split,
     AI Baseline Demand, 7-Day Alk Demand Learning, 7-Day Calcium Demand
     Learning, and Chemical Recipes -> Strengths. Confirmed via source
     inspection that the legacy AIEngine class they all fed is never
     included, instantiated, or called anywhere in the current firmware --
     not "disconnected," genuinely dead code, safe to remove. Their
     backend WebRoutes.cpp handlers and main.cpp globals were intentionally
     left in place (lower risk than auditing every reference tonight) --
     harmless unreachable code now that nothing in the UI calls them.
     Real v2 equivalents: per-chemical potency/cap/night-dosing/day-avoid
     settings are under Manage Chemicals; day/night behavior is per-
     chemical (daytimeSuppressPercent), not a global split.

     CORRECTION 2026-08-04: "AI Chemistry Safeties" was NOT fully dead as
     first believed -- 3 of its 8 fields genuinely feed the live v2 safety
     envelope (confirmed via ai.safety.*.updateSuggestion() call sites in
     main.cpp, not just the empty applyAiChemistrySafetiesToEngine() stub
     that made the whole card look dead at a glance):
       aiMaxAlkRiseDkhDay      -> ai.safety.maxAlkRisePerDayDkh
       aiMaxCaRisePpmDay       -> ai.safety.maxCaRisePerDayPpm  (had NO UI at all, even before tonight)
       aiMaxMgCorrectionDayMl  -> ai.safety.maxMgRisePerDayPpm
     The other 5 fields (Max Kalk/NaOH/Alk Solution Per Day, Max Mg Per
     Day, Mg Deadband) really are dead -- per-chemical maxMlPerDay
     (Manage Chemicals) is what actually caps those now, and Mg Deadband
     has no v2 consumer at all (main.cpp's own comment admits this).
     Restored below as a small, accurately-scoped card for just the 3
     live fields, reusing the same unchanged backend endpoint
     (/api/config/ai-chemistry-safeties already falls back to the current
     value for any field not present in the request body, so submitting
     only these 3 is safe and does not disturb the 5 dead ones' stored
     values). -->

<div class="card" style="grid-column:1 / -1">
  <div class="card-title">
    <h3>Dosing Safety Rise Limits</h3>
    <span class="meta">Live v2 safety envelope</span>
  </div>
  <div class="help">These three values genuinely cap how fast the current dosing engine is allowed to correct each parameter per day, independent of per-chemical daily caps under Manage Chemicals. (The rest of the old "AI Chemistry Safeties" card was confirmed dead and was not restored -- per-chemical caps under Manage Chemicals replace it.)</div>
  <div class="three" style="margin-top:12px;">
    <div>
      <div class="help" style="margin-bottom:8px;">Max Alk Rise (dKH/day)</div>
      <input type="number" id="aiSafeMaxAlkRise" step="0.01" min="0.01">
    </div>
    <div>
      <div class="help" style="margin-bottom:8px;">Max Ca Rise (ppm/day)</div>
      <input type="number" id="aiSafeMaxCaRise" step="0.1" min="0.1">
    </div>
    <div>
      <div class="help" style="margin-bottom:8px;">Max Mg Correction (mL/day)</div>
      <input type="number" id="aiSafeMaxMgCorrection" step="1" min="0">
    </div>
  </div>
  <button class="sec" onclick="saveAiRiseSafeties()" style="margin-top:14px">Update Rise Limits</button>
</div>

<div class="card" style="grid-column: 1 / -1">
  <div class="card-title">
    <h3>Dosing History</h3>
    <span class="meta" id="dosingHistoryMeta">Actual dosed mL/day</span>
  </div>
  <div class="history-toolbar">
    <select id="historyRange" class="history-select" onchange="loadDosingHistory(true)">
      <option value="14">Last 14 days</option>
      <option value="30" selected>Last 30 days</option>
      <option value="60">Last 60 days</option>
    </select>
    <button class="sec" onclick="loadDosingHistory(true)">Refresh History</button>
  </div>
  <div class="history-summary" id="dosingHistorySummary"></div>
  <div style="height:360px;"><canvas id="dosingHistoryChart"></canvas></div>
  <div class="footer-note">This graph shows <b>actual dispensed pump totals</b> from daily history records, not the AI plan and not the pending buckets. Today comes from the controller's live <code>/api/history</code>; older days are loaded from Firebase daily records when this page can access RTDB.</div>
</div>

<div class="card" style="grid-column: 1 / -1">
  <div class="card-title">
    <h3>Realtime Analytics</h3>
    <span class="meta" id="realtimeChartMeta">Live browser graph • no Firebase writes</span>
  </div>
  
  <div class="chart-stack">
    <div>
      <h4 style="color:var(--accent); font-size: 0.75rem; text-transform: uppercase; margin-bottom:10px;">1) Realtime Water Parameters</h4>
      <div style="height:280px;"><canvas id="paramsChart"></canvas></div>
    </div>

    <div>
      <h4 style="color:var(--accent); font-size: 0.75rem; text-transform: uppercase; margin-bottom:10px;">2) Realtime Buckets / Pending mL</h4>
      <div style="height:250px;"><canvas id="dosingChart"></canvas></div>
    </div>

    <div>
      <h4 style="color:var(--accent); font-size: 0.75rem; text-transform: uppercase; margin-bottom:10px;">3) Realtime AI Plan - Active Chemicals Only (mL/day)</h4>
      <div style="height:250px;"><canvas id="planChart"></canvas></div>
    </div>
  </div>
  <div class="footer-note">These graphs are built from the dashboard's 5-second <code>/api/status</code> refresh and are kept only in browser memory. Samples older than about 30 days are purged automatically. Firebase still only gets throttled state/plan writes and the midnight daily summary.</div>
</div>

<script>
  // §5 free chemical declaration -- replaces the mode picker (see
  // DASHBOARD_MIGRATION_PLAN.md). declaredChemicalsCache mirrors
  // GET /api/chemicals; chemicalPresetsCache mirrors GET /api/chemical-presets.
  // Both are the single source of truth for pump wiring now -- no more
  // hardcoded DOSING_MODES table duplicating what main.cpp already knows.
  let declaredChemicalsCache = [];
  let chemicalPresetsCache = [];

  async function loadDeclaredChemicals(){
    try {
      const data = await api('/api/chemicals');
      if (data.ok) declaredChemicalsCache = data.chemicals || [];
    } catch(e) { console.error('loadDeclaredChemicals failed', e); }
    renderManageChemicals();
    renderPumpSelect();
    if (!anyCalibrationInputActiveOrDirty()) renderCalibration(null, currentStatus.flowMlPerMin || {});
  }

  // Added 2026-08-04: a real, confirmed UX gap -- a successful action
  // (e.g. removing a chemical) previously gave NO positive feedback at
  // all, just a card silently disappearing from the list. Indistinguishable
  // from nothing having happened, which is exactly what caused real
  // confusion tonight: a genuinely successful Remove was reported as
  // "nothing happened" because there was nothing telling the person
  // otherwise. Simple, unobtrusive toast -- appears briefly, fades on its
  // own, doesn't block anything.
  function showToast(message, isError){
    let el = document.getElementById('toastNotification');
    if (!el) {
      el = document.createElement('div');
      el.id = 'toastNotification';
      el.style.cssText = 'position:fixed; bottom:24px; left:50%; transform:translateX(-50%); ' +
        'padding:12px 20px; border-radius:10px; font-weight:700; z-index:9999; ' +
        'box-shadow:0 4px 16px rgba(0,0,0,.4); transition:opacity .3s; max-width:80vw; text-align:center;';
      document.body.appendChild(el);
    }
    el.style.background = isError ? 'var(--danger, #c0392b)' : 'var(--accent, #2ecc71)';
    el.style.color = '#fff';
    el.textContent = message;
    el.style.opacity = '1';
    clearTimeout(el._hideTimer);
    el._hideTimer = setTimeout(() => { el.style.opacity = '0'; }, 3000);
  }

  async function loadChemicalPresets(){
    try {
      const data = await api('/api/chemical-presets');
      if (data.ok) chemicalPresetsCache = data.presets || [];
    } catch(e) { console.error('loadChemicalPresets failed', e); }
    populatePresetDropdown();
  }

  // Replaces the old DOSING_MODES[mode] lookup. `mode` is accepted but
  // ignored -- kept so existing call sites (renderPumpSelect,
  // renderCalibration, renderActivePlan, pumpChemicalName) don't all need
  // signature changes during the transition. Pump wiring now comes
  // entirely from declaredChemicalsCache, sorted by physical pump index.
  function getModeCfg(mode){
    const pumps = declaredChemicalsCache
      .slice()
      .sort((a, b) => a.pumpIndex - b.pumpIndex)
      .map(c => ({
        index: c.pumpIndex,
        key: 'pump' + (c.pumpIndex + 1),
        name: c.name + ' (Pump ' + (c.pumpIndex + 1) + ')'
      }));
    return { title: declaredChemicalsCache.length + ' chemical(s) declared', pumps };
  }

  let currentStatus = {};
  let chemicalDirty = false;
  let currentMode = 1;
  let currentDosingMode = 1;
  let lightDirty = false;

  // ===== Firebase Web Push setup =====
  // Fill these from Firebase Console > Project settings > General / Cloud Messaging.
  // This only works from Firebase Hosting / HTTPS. It will not work from http://ESP32-IP.
  const FIREBASE_WEB_CONFIG = {
    apiKey: "AIzaSyB4XtC5Pvxw6To58EKTLMADQLqR_hTZK0M",
    authDomain: "aiesdoser.firebaseapp.com",
    databaseURL: "https://aiesdoser-default-rtdb.firebaseio.com",
    projectId: "aiesdoser",
    storageBucket: "aiesdoser.appspot.com",
    messagingSenderId: "449222824743",
    appId: "1:449222824743:web:06e34fc591d5af0f1a8f02"
  };
const FIREBASE_WEB_PUSH_VAPID_KEY = "BNEHGv71r2Ac8cvVOtthjvCJfPPGeYC-IUEOesBK_EFU4yCASj-LTlx7yeei-8HgsPxo1HUanXStPrdUQZF20aw";
  let firebasePushInitialized = false;


  let notificationDirty = false;
  function markNotificationDirty(){ notificationDirty = true; }
  function clearNotificationDirty(){ notificationDirty = false; }
  function isNotificationEditing(){
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    return notificationDirty || activeId === 'notificationLevelSelect';
  }

  function markLightDirty(){ lightDirty = true; }
  function clearLightDirty(){ lightDirty = false; }
  function isLightEditing(){
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    return lightDirty || activeId === 'lightSrc' || activeId === 'lStart' || activeId === 'lEnd' || activeId === 'lOutlet';
  }

  // Prevent the 5-second refresh from overwriting Dosing Safeties while typing.
  // Once either safety field is focused/edited, both fields are protected until Save succeeds
  // or the user tabs/clicks away without changes.
  let safetyDirty = false;
  function markSafetyDirty(){ safetyDirty = true; }
  function clearSafetyDirty(){ safetyDirty = false; }
  function isSafetyEditing(){
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    return safetyDirty || activeId === 'dTresh' || activeId === 'dMax' || activeId.startsWith('safeThr') || activeId.startsWith('safeMax') || activeId.startsWith('safeDay');
  }

  let aiChemSafetyDirty = false;
  function markAiChemSafetyDirty(){ aiChemSafetyDirty = true; }
  function clearAiChemSafetyDirty(){ aiChemSafetyDirty = false; }
  function isAiChemSafetyEditing(){
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    return aiChemSafetyDirty || activeId.startsWith('aiSafe');
  }

  let mode7SplitDirty = false;
  function markMode7SplitDirty(){ mode7SplitDirty = true; }
  function clearMode7SplitDirty(){ mode7SplitDirty = false; }
  function isMode7SplitEditing(){
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    return mode7SplitDirty || activeId === 'm7SplitEnabled' || activeId === 'm7NaohMaxPh' || activeId.startsWith('m7Day') || activeId.startsWith('m7Night');
  }

  let calciumDemandLearningDirty = false;
  function markCalciumDemandLearningDirty(){ calciumDemandLearningDirty = true; }
  function clearCalciumDemandLearningDirty(){ calciumDemandLearningDirty = false; }
  function isCalciumDemandLearningEditing(){
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    return calciumDemandLearningDirty || activeId === 'calciumDemandLearningEnabled';
  }

  let alkDemandLearningDirty = false;
  function markAlkDemandLearningDirty(){ alkDemandLearningDirty = true; }
  function clearAlkDemandLearningDirty(){ alkDemandLearningDirty = false; }
  function isAlkDemandLearningEditing(){
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    return alkDemandLearningDirty || activeId === 'alkDemandLearningEnabled';
  }

  let aiBaselineDirty = false;
  function markAiBaselineDirty(){ aiBaselineDirty = true; }
  function clearAiBaselineDirty(){ aiBaselineDirty = false; }
  function isAiBaselineEditing(){
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    return aiBaselineDirty || activeId === 'coralLoad' || activeId === 'baseKalk' || activeId === 'baseCacl2' || activeId === 'baseNaoh' || activeId === 'baseMg';
  }

  let chemicalStrengthDirty = false;
  function markChemicalStrengthDirty(){ chemicalStrengthDirty = true; }
  function clearChemicalStrengthDirty(){ chemicalStrengthDirty = false; }
  function isChemicalStrengthEditing(){
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    return chemicalStrengthDirty || activeId === 'strKalk' || activeId === 'strAfr' || activeId === 'strAlk' || activeId === 'strNaoh' || activeId === 'strMg' || activeId === 'strCacl2';
  }

  // Prevent the 5-second status refresh from rebuilding calibration inputs
  // while you are typing measured output for any of the 4 pumps.
  const calibrationDirty = [false, false, false, false];
  const calibrationLastRunSec = [60, 60, 60, 60];

  function markCalibrationDirty(pumpIndex){
    if (pumpIndex >= 0 && pumpIndex < calibrationDirty.length) calibrationDirty[pumpIndex] = true;
  }

  function clearCalibrationDirty(pumpIndex){
    if (pumpIndex >= 0 && pumpIndex < calibrationDirty.length) calibrationDirty[pumpIndex] = false;
  }

  function anyCalibrationInputActiveOrDirty(){
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    if (activeId.startsWith('flow_') || activeId.startsWith('cal_sec_')) return true;
    return calibrationDirty.some(Boolean);
  }

  async function api(url, method='GET', data=null){
    const opts = { method, headers:{} };
    if(data !== null){
      opts.headers['Content-Type'] = 'application/json';
      opts.body = JSON.stringify(data);
    }
    const res = await fetch(url, opts);
    const text = await res.text();
    try { return JSON.parse(text); } catch(_) { return { ok: res.ok, raw: text }; }
  }

  function safeNum(v, digits=2){
    const n = Number(v);
    return Number.isFinite(n) ? n.toFixed(digits) : '--';
  }

  function normalizeNotificationLevel(level){
    const v = String(level || 'warning').toLowerCase();
    return ['muted','severe','warning','info'].includes(v) ? v : 'warning';
  }

  function notificationLevelLabel(level){
    switch (normalizeNotificationLevel(level)) {
      case 'muted': return 'Muted';
      case 'severe': return 'Severe only';
      case 'warning': return 'Severe + Warning';
      case 'info': return 'All alerts';
      default: return 'Severe + Warning';
    }
  }

async function saveVol() {
  const gal = parseFloat(document.getElementById('tankGal').value);
  if (!Number.isFinite(gal) || gal <= 0) {
    alert('Enter a valid tank volume in gallons.');
    return;
  }
  await fetch('/api/config/volume', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ gallons: gal, volume: gal * 3.78541 })
  });
  await loadAll();
  updateRecipePreview();
  alert("System volume set to " + gal.toFixed(1) + " Gallons");
}

  function renderPumpSelect(mode){
    const sel = document.getElementById('pSel');
    if (!sel) return;

    const cfg = getModeCfg(mode);
    const previousValue = sel.value;
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';

    // Fixed 2026-08-04: previously only guarded while focus was literally
    // ON the dropdown. The moment a user picked a pump, then clicked into
    // the mL amount field (v_ml) -- a completely normal next step -- focus
    // left 'pSel', and the next 5-second background refresh could silently
    // rebuild this dropdown back to its default (Pump 1), per this
    // function's own original comment above admitting that exact failure
    // mode. Clicking "Run Live Dose" after that silently read back Pump 1,
    // not whatever the user actually selected -- with no visual sign
    // anything had changed. Widened to also protect while typing the dose
    // amount, closing the most common real-world version of that race.
    if (activeId === 'pSel' || activeId === 'v_ml') return;

    const nextHtml = cfg.pumps.map(p => `<option value="${p.index}">${p.name}</option>`).join('');

    // Only rewrite the options if the dosing implementation changed.
    // This prevents normal status refreshes from resetting the selected pump.
    if (sel.dataset.mode !== String(mode) || sel.dataset.html !== nextHtml) {
      sel.innerHTML = nextHtml;
      sel.dataset.mode = String(mode);
      sel.dataset.html = nextHtml;
    }

    // Restore the user's selected pump if it is still valid for this dosing mode.
    if ([...sel.options].some(o => o.value === previousValue)) {
      sel.value = previousValue;
    }
  }

  function renderCalibration(mode, flows){
    const cfg = getModeCfg(mode);
    const wrap = document.getElementById('calibrationContainer');
    wrap.innerHTML = cfg.pumps.map(p => {
      // Fixed 2026-07-25: was looking up flows[p.key] ("pump1", "pump2",
      // ...) but /api/status actually reports flow rates keyed "p1"/"p2"/
      // "p3"/"p4" -- those never matched, so this always showed 0.00
      // regardless of what was actually saved. p.key is still the right
      // thing to use for calibration-run labeling elsewhere; this is a
      // separate, differently-keyed field.
      const flow = Number((flows || {})['p' + (p.index + 1)]);
      const displayFlow = Number.isFinite(flow) ? flow.toFixed(2) : '0.00';
      const lastSec = calibrationLastRunSec[p.index] || 60;
      return `
        <div class="cal-item">
          <div class="cal-top">
            <div>
              <div class="cal-name">${p.name}</div>
              <div class="cal-sub">${p.key.toUpperCase()} • physical pump ${p.index + 1}</div>
            </div>
            <div class="flow-badge" id="flow_badge_${p.index}">${displayFlow} mL/min</div>
          </div>
          <div class="two">
            <input type="number" step="0.1" id="flow_${p.index}" value="" placeholder="Measured mL collected" onfocus="markCalibrationDirty(${p.index})" oninput="markCalibrationDirty(${p.index})">
            <input type="number" step="1" min="1" id="cal_sec_${p.index}" value="${lastSec}" placeholder="Run seconds" onfocus="markCalibrationDirty(${p.index})" oninput="markCalibrationDirty(${p.index})">
          </div>
          <div class="three" style="margin-top:10px">
            <button onclick="runCalibrationPump(${p.index}, '${p.key}', 30)">Run 30 sec</button>
            <button onclick="runCalibrationPump(${p.index}, '${p.key}', 60)">Run 60 sec</button>
            <button class="sec" onclick="saveCalibration(${p.index}, '${p.key}')">Save ${p.key.toUpperCase()} Flow</button>
          </div>
          <div class="help" style="margin-top:8px">Current saved flow is shown on the right. Run into a measuring cup, enter the actual mL collected and the run seconds, then save.</div>
        </div>
      `;
    }).join('');
  }


  // LOCAL DASHBOARD PLAN SOURCE
  // This page is served by the ESP32, so the Active Dosing Plan must come from
  // /api/status only.  Firebase is not used for this card.
  function planFromStatus(s){
    if (!s) return {};
    // §5 free chemical declaration: dosingMlPerDayByPump is keyed "pump1".."pump4"
    // and correct for ANY declared chemical, not just the six legacy names
    // dosingMlPerDay assumes. Preferred; legacy field kept as a fallback
    // only for talking to older firmware during the transition.
    return s.dosingMlPerDayByPump || s.dosingMlPerDay || s.aiPlan || s.plan || s.currentPlan || {};
  }

  function activePlanKeys(mode){
    return getModeCfg(mode).pumps.map(p => p.key);
  }

  function aliasKeysForPlan(key){
    return {
      ca: ['ca','cacl2'],
      cacl2: ['cacl2','ca'],
      alk: ['alk'],
      naoh: ['naoh'],
      kalk: ['kalk'],
      afr: ['afr'],
      mg: ['mg'],
      tbd: ['tbd','aux'],
      aux: ['aux','tbd']
    }[key] || [key];
  }

  function planValue(plan, key){
    for (const k of aliasKeysForPlan(key)) {
      const n = numOrNull(plan && plan[k]);
      if (n !== null) return n;
    }
    return 0;
  }

  function bucketValueForPlanFallback(s, pumpIndex, chemicalKey){
    const b = s?.buckets || s?.pendingMl || s?.pending || {};
    const physicalAliases = {
      0: ['p1','P1','pump1','0'],
      1: ['p2','P2','pump2','1'],
      2: ['p3','P3','pump3','2'],
      3: ['p4','P4','pump4','3']
    };

    for (const k of (physicalAliases[pumpIndex] || [])) {
      const n = numOrNull(b[k]);
      if (n !== null && n > 0) return n;
    }

    for (const k of aliasKeysForPlan(chemicalKey)) {
      const n = numOrNull(b[k]);
      if (n !== null && n > 0) return n;
    }

    return 0;
  }

  function baselineValueForPlanFallback(s, key){
    const base = s?.aiBaseline || {};
    if (key === 'alk' && Number(s?.dosingMode ?? currentDosingMode ?? 1) === 7) {
      // In Mode 7 the old Mg baseline field is intentionally reused as the Alk baseline.
      const mode7Alk = numOrNull(base.alk ?? base.mg);
      return mode7Alk !== null ? mode7Alk : 0;
    }
    for (const k of aliasKeysForPlan(key)) {
      const n = numOrNull(base[k]);
      if (n !== null) return n;
    }
    return 0;
  }

function localPlanValue(s, pump){
  const plan = planFromStatus(s);
  const direct = planValue(plan, pump.key);

  return {
    value: direct,
    source: direct > 0 ? 'live plan' : 'no active plan'
  };
}
  function renderActivePlan(s){
    const list = document.getElementById('activePlanList');
    if (!list) return;
    const mode = Number(s.dosingMode ?? currentDosingMode ?? 1);
    const cfg = getModeCfg(mode);
    const rowSources = new Set();

    list.innerHTML = cfg.pumps.map(p => {
      const resolved = localPlanValue(s, p);
      rowSources.add(resolved.source);
      const cleanName = p.name.replace(/ \(Pump \d\)/,'');
      return `<div class="plan-row">
        <div class="plan-left"><span class="dot"></span><div><div class="plan-name">${cleanName}</div><div class="plan-sub">ml per day • ${resolved.source}</div></div></div>
        <div class="plan-amt">${resolved.value.toFixed(2)}</div>
      </div>`;
    }).join('');

    const meta = document.getElementById('planRealtimeMeta');
    if (meta) meta.textContent = 'Local /api/status • ' + Array.from(rowSources).join(', ') + ' • ' + new Date().toLocaleTimeString();
  }

  function pumpChemicalName(index, mode){
    const cfg = getModeCfg(mode || currentDosingMode || 1);
    const p = (cfg.pumps || []).find(x => x.index === index);
    if (p) return p.name.replace(/ \(Pump \d\)/,'');
    return 'Pump ' + (index + 1);
  }

  function chemicalStatusBadge(r){
    if (!r || !r.enabled) return '<span class="chem-badge">Disabled</span>';
    if (r.severe) return '<span class="chem-badge severe">Severe</span>';
    if (r.warning) return '<span class="chem-badge warn">Warning</span>';
    return '<span class="chem-badge">OK</span>';
  }

  function chemicalLevelOptions(currentGal, capacityGal){
    const opts = [];
    opts.push(`<option value="-1">No change</option>`);
    opts.push(`<option value="0" ${Number(currentGal) <= 0 && Number(capacityGal) > 0 ? 'selected' : ''}>Empty</option>`);
    for (let g = 0.5; g <= 5.001; g += 0.5) {
      const selected = Math.abs(Number(currentGal || 0) - g) < 0.05 ? 'selected' : '';
      opts.push(`<option value="${g.toFixed(1)}" ${selected}>${g.toFixed(1)} gal</option>`);
    }
    opts.push(`<option value="full">Full</option>`);
    return opts.join('');
  }

  function renderChemicalLevels(s){
    const grid = document.getElementById('chemicalLevelGrid');
    if (!grid) return;

    const levels = s.chemicalLevels || {};
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    const editing = activeId.startsWith('chemCap_') || activeId.startsWith('chemCurrent_');

    let html = '';
    for (let i = 0; i < 4; i++) {
      const key = 'p' + (i + 1);
      const r = levels[key] || {};
      const cap = Number(r.capacityGal || 0);
      const remainingGal = Number(r.remainingGal || 0);
      const pct = Number(r.remainingPct || 0);
      const capOptions = [0,1,2,3,4,5].map(g => `<option value="${g}" ${Math.round(cap) === g ? 'selected' : ''}>${g === 0 ? 'Disabled' : g + ' gal'}</option>`).join('');
      const currentOptions = chemicalLevelOptions(remainingGal, cap);

      const cleanPct = cap > 0 ? Math.max(0, Math.min(100, pct)) : 0;
      const barClass = r.severe ? 'severe' : (r.warning ? 'warn' : '');
      const levelText = cap > 0
        ? `${remainingGal.toFixed(2)} gal left of ${cap.toFixed(1)} gal`
        : 'Reservoir tracking disabled';

      html += `<div class="chem-item">
        <div class="chem-top">
          <div>
            <div class="chem-name">P${i + 1} ${pumpChemicalName(i, currentDosingMode)}</div>
            <div class="chem-left">${levelText}</div>
          </div>
          ${chemicalStatusBadge(r)}
        </div>

        <div class="chem-level-meta">
          <span>Reservoir level</span>
          <span class="chem-level-percent">${cap > 0 ? cleanPct.toFixed(0) + '%' : '--'}</span>
        </div>
        <div class="chem-level-bar" title="${levelText}">
          <div class="chem-level-fill ${barClass}" style="width:${cleanPct.toFixed(0)}%"></div>
        </div>

        <div class="two">
          <div>
            <div class="help" style="margin-bottom:6px;">Capacity</div>
            <select id="chemCap_${i}" onfocus="markChemicalDirty()" onchange="markChemicalDirty()">${capOptions}</select>
          </div>
          <div>
            <div class="help" style="margin-bottom:6px;">Current Level</div>
            <select id="chemCurrent_${i}" onfocus="markChemicalDirty()" onchange="markChemicalDirty()">${currentOptions}</select>
          </div>
        </div>
      </div>`;
    }

    if (!editing && !chemicalDirty) {
      grid.innerHTML = html;
    } else if (!grid.innerHTML.trim()) {
      grid.innerHTML = html;
    }
  }

  function markChemicalDirty(){ chemicalDirty = true; }

  // §1/§3.2 "reduce required manual testing frequency over time... actively
  // prompts if 7+ days have passed with no manual cross-check." Engine
  // computes everything (main.cpp's manualTestPrompt.* fields in
  // /api/status); this just renders it. Hidden entirely for Apex-equipped
  // tanks (applicable=false) -- they're already cross-checking constantly
  // via automated testing, so this card would be noise for them.
  function renderManualTestPrompt(s){
    const card = document.getElementById('manualTestPromptCard');
    if (!card) return;

    const prompt = s.manualTestPrompt || {};
    if (!prompt.applicable) {
      card.style.display = 'none';
      return;
    }
    card.style.display = '';

    const daysSince = Number(prompt.daysSinceLastTest);
    const recommendedDays = Number(prompt.recommendedIntervalDays);
    const maturity = Number(prompt.maturity);
    const needed = !!prompt.needed;

    const daysSinceText = (Number.isFinite(daysSince) && daysSince >= 0)
      ? (daysSince === 0 ? 'Today' : daysSince === 1 ? '1 day ago' : daysSince + ' days ago')
      : 'No test recorded yet';
    document.getElementById('manualTestDaysAgo').textContent = daysSinceText;

    // Named tiers, matching the three discrete stages
    // recommendedManualTestIntervalDays() now returns (daily / every other
    // day / weekly) -- reads more naturally than "Every 1 days".
    const intervalText = recommendedDays === 1 ? 'Daily'
      : recommendedDays === 2 ? 'Every other day'
      : recommendedDays === 7 ? 'Weekly'
      : Number.isFinite(recommendedDays) ? 'Every ' + recommendedDays + ' days'
      : '--';
    document.getElementById('manualTestInterval').textContent = intervalText;

    const pill = document.getElementById('manualTestPromptPill');
    pill.textContent = needed ? 'Test recommended' : 'On track';

    const maturityPct = Number.isFinite(maturity) ? Math.round(Math.max(0, Math.min(1, maturity)) * 100) : 0;
    document.getElementById('manualTestMaturityPct').textContent = maturityPct + '%';
    const fill = document.getElementById('manualTestMaturityFill');
    fill.style.width = maturityPct + '%';
    fill.className = 'chem-level-fill' + (needed ? ' warn' : '');

    const note = document.getElementById('manualTestPromptNote');
    note.textContent = needed
      ? 'It\u2019s been a while since your last test \u2014 a fresh reading helps keep the AI\u2019s model accurate.'
      : 'As the AI proves it can predict your tank between tests, this interval stretches from daily toward weekly.';
  }

  // Added 2026-08-04, §4.2/§4.3: genuine progress bars toward real 7-day
  // (Week) and 30-day (Month) data collection, per parameter, using
  // AIEngineV2's own history buffer -- unlike the removed legacy 7-Day
  // Alk/Calcium Demand Learning cards, this reads real, live data.
  function renderWeekMonthTrend(s){
    const wrap = document.getElementById('weekMonthTrendRows');
    if (!wrap) return;
    const data = s.weekMonthTrend || {};
    const labels = { alk: 'Alkalinity', ca: 'Calcium', mg: 'Magnesium' };

    let html = '';
    for (const key of ['alk', 'ca', 'mg']) {
      const d = data[key] || {};
      const spanDays = Number(d.historySpanDays) || 0;
      const weekPct = Math.max(0, Math.min(1, spanDays / 7)) * 100;
      const monthPct = Math.max(0, Math.min(1, spanDays / 30)) * 100;

      const weekStatus = d.hasWeekData
        ? (Number(d.weekTrendPerDay) >= 0 ? '+' : '') + Number(d.weekTrendPerDay).toFixed(4) + '/day'
        : 'Collecting\u2026 (' + weekPct.toFixed(0) + '%)';
      const monthStatus = d.hasMonthData
        ? (Number(d.monthTrendPerDay) >= 0 ? '+' : '') + Number(d.monthTrendPerDay).toFixed(4) + '/day'
        : 'Collecting\u2026 (' + monthPct.toFixed(0) + '%)';

      const driftBadge = d.isDrifting
        ? '<span class="status-pill warn" style="margin-left:8px">DRIFTING</span>' : '';
      const anomalyBadge = d.wasAnomaly
        ? '<span class="status-pill warn" style="margin-left:8px">last result: ANOMALY</span>' : '';

      html += `
        <div style="margin-top:16px; padding-top:12px; border-top:1px solid var(--border);">
          <div class="chem-level-meta"><span><b>${labels[key]}</b>${driftBadge}${anomalyBadge}</span></div>

          <div class="chem-level-meta" style="margin-top:8px"><span>Week trend (7-day)</span><span class="chem-level-percent">${weekStatus}</span></div>
          <div class="chem-level-bar" title="Real logged measurements over the trailing 7 days">
            <div class="chem-level-fill" style="width:${weekPct}%"></div>
          </div>

          <div class="chem-level-meta" style="margin-top:8px"><span>Month trend (30-day)</span><span class="chem-level-percent">${monthStatus}</span></div>
          <div class="chem-level-bar" title="Real logged measurements over the trailing 30 days">
            <div class="chem-level-fill" style="width:${monthPct}%"></div>
          </div>
        </div>`;
    }
    wrap.innerHTML = html;
  }

  function isChemicalEditing(){
    const id = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    return chemicalDirty || id.startsWith('chemCap_') || id.startsWith('chemCurrent_');
  }

  async function saveChemicalCapacity(){
    const body = { resetRemaining:false, setCurrent:false };
    for (let i = 0; i < 4; i++) {
      const el = document.getElementById('chemCap_' + i);
      body['p' + (i + 1) + 'Gal'] = el ? Number(el.value || 0) : 0;
    }
    const res = await api('/api/chemical-levels', 'POST', body);
    chemicalDirty = false;
    await loadAll();
    alert('Chemical reservoir capacities saved. Current levels were not reset.');
    return res;
  }

  async function setChemicalCurrentLevel(){
    const body = { resetRemaining:false, setCurrent:true };
    for (let i = 0; i < 4; i++) {
      const capEl = document.getElementById('chemCap_' + i);
      const curEl = document.getElementById('chemCurrent_' + i);
      const capGal = capEl ? Number(capEl.value || 0) : 0;
      let currentVal = curEl ? curEl.value : '-1';

      body['p' + (i + 1) + 'Gal'] = capGal;

      if (currentVal === 'full') {
        body['p' + (i + 1) + 'CurrentGal'] = capGal;
      } else if (currentVal !== '-1') {
        body['p' + (i + 1) + 'CurrentGal'] = Number(currentVal || 0);
      }
    }
    const res = await api('/api/chemical-levels', 'POST', body);
    chemicalDirty = false;
    await loadAll();
    alert('Chemical current levels saved.');
    return res;
  }

  async function fillChemicalToFull(){
    const body = { resetRemaining:true };
    for (let i = 0; i < 4; i++) {
      const el = document.getElementById('chemCap_' + i);
      body['p' + (i + 1) + 'Gal'] = el ? Number(el.value || 0) : 0;
    }
    const res = await api('/api/chemical-levels', 'POST', body);
    chemicalDirty = false;
    await loadAll();
    alert('Chemical reservoirs set to full.');
    return res;
  }

  // Backward-compatible wrapper for older dashboard buttons/cache.
  async function saveChemicalLevels(resetRemaining=true){
    return resetRemaining ? fillChemicalToFull() : saveChemicalCapacity();
  }

  function renderStatus(s){
    currentStatus = s || {};
    currentMode = Number(s.mode ?? 1);
    currentDosingMode = Number(s.dosingMode ?? 1);
    renderActivePlan(s);
    renderChemicalLevels(s);
    renderManualTestPrompt(s);
    renderWeekMonthTrend(s);
    checkSetupWizardVisibility(s);

    // Restored 2026-08-04, see the inline comment at this card's HTML for
    // the full incident -- only these 3 fields are genuinely live.
    (function renderAiRiseSafeties() {
      const safeties = s.aiChemistrySafeties || {};
      const setIfIdle = (id, value) => {
        const el = document.getElementById(id);
        if (el && document.activeElement !== el && value !== undefined && value !== null) {
          el.value = value;
        }
      };
      setIfIdle('aiSafeMaxAlkRise', safeties.maxAlkRiseDkhDay);
      setIfIdle('aiSafeMaxCaRise', safeties.maxCaRisePpmDay);
      setIfIdle('aiSafeMaxMgCorrection', safeties.maxMgCorrectionDayMl);
    })();


    document.getElementById('sensorPh').innerHTML = `${safeNum(s.ph, 2)}`;
    document.getElementById('sensorTemp').innerHTML = `${safeNum((s.temp ?? s.tempF), 1)} <span class="unit">°F</span>`;    document.getElementById('sensorAlk').innerHTML = `${safeNum(s.alk, 2)} <span class="unit">dKH</span>`;
    document.getElementById('sensorCa').innerHTML = `${safeNum(s.ca, 1)} <span class="unit">ppm</span>`;
    document.getElementById('sensorMg').innerHTML = `${safeNum(s.mg, 1)} <span class="unit">ppm</span>`;
    document.getElementById('sensorSal').innerHTML = `${safeNum(s.ppt, 1)} / <span class="unit">${safeNum(s.sg, 3)}</span>`;

    document.querySelectorAll('.mode-btn').forEach(b => b.classList.remove('active'));
    const activeBtn = document.getElementById('m' + currentMode);
    if(activeBtn) activeBtn.classList.add('active');

    document.getElementById('opModeText').textContent = currentMode === 0 ? 'OFF' : currentMode === 1 ? 'AUTO' : 'MAN';
    document.getElementById('eStopBtn').textContent = s.stop ? '✅ Clear Emergency Stop' : '🛑 Emergency Stop';

    const notificationLevel = normalizeNotificationLevel(s.notificationLevel ?? s.alertMode ?? s.settings?.notificationLevel);
    const notificationSelect = document.getElementById('notificationLevelSelect');
    const notificationPill = document.getElementById('notificationLevelPill');
    if (!isNotificationEditing() && notificationSelect) notificationSelect.value = notificationLevel;
    if (notificationPill) notificationPill.textContent = notificationLevelLabel(notificationLevel);

    renderPumpSelect(currentDosingMode);
    // Do not rebuild calibration inputs while the user is typing measured output.
    if (!anyCalibrationInputActiveOrDirty()) {
      renderCalibration(currentDosingMode, s.flowMlPerMin || {});
    }

    document.getElementById('apEn').value = s.apexEnabled ? '1' : '0';
    document.getElementById('apIp').value = s.apexIp || '';
    document.getElementById('apexStatePill').textContent = s.apexEnabled ? 'Enabled' : 'Disabled';

    // Load saved manual/Apex light schedule from firmware without overwriting while user is editing.
    populateHours();
    const light = s.lightConfig || {};
    if (!isLightEditing()) {
      const lightSrcEl = document.getElementById('lightSrc');
      const lStartEl = document.getElementById('lStart');
      const lEndEl = document.getElementById('lEnd');
      const lOutletEl = document.getElementById('lOutlet');
      if (lightSrcEl) lightSrcEl.value = String(light.source ?? 0);
      if (lStartEl) lStartEl.value = String(light.start ?? 8);
      if (lEndEl) lEndEl.value = String(light.end ?? 20);
      if (lOutletEl) lOutletEl.value = String(light.outlet || '');
      uiToggleLights();
    }

    document.getElementById('deviceChip').textContent = `Device: ${s.deviceId || 'Local'}`;
    document.getElementById('onlineText').textContent = s.wifiConnected ? 'WiFi Connected' : 'WiFi Offline';
    document.getElementById('onlineDot').style.background = s.wifiConnected ? 'var(--success)' : 'var(--danger)';
    document.getElementById('onlineDot').style.boxShadow = s.wifiConnected ? '0 0 10px rgba(79,224,168,.8)' : '0 0 10px rgba(255,84,112,.8)';
    document.getElementById('syncBox').textContent = 'Last local sync: ' + new Date().toLocaleTimeString();
    // Do not overwrite safety inputs while the user is changing them.
    if (!isSafetyEditing()) {
      renderPumpSafetyInputs(s);
    }
    if (!isAiChemSafetyEditing()) {
      renderAiChemistrySafeties(s);
    }
    if (!isMode7SplitEditing()) {
      renderMode7DayNightSplit(s);
    }


    const baseline = s.aiBaseline || {};
    const galForBaseline = Number(s.tankGallons ?? s.tankGal ?? s.gallons);
    const hintEl = document.getElementById('baselineTankHint');
    if (hintEl) hintEl.value = Number.isFinite(galForBaseline) && galForBaseline > 0 ? galForBaseline.toFixed(1) + ' gallons' : '-- gallons';
    if (!isAiBaselineEditing()) {
      const loadEl = document.getElementById('coralLoad');
      const baseKalkEl = document.getElementById('baseKalk');
      const baseCacl2El = document.getElementById('baseCacl2');
      const baseNaohEl = document.getElementById('baseNaoh');
      const baseMgEl = document.getElementById('baseMg');
      if (loadEl) loadEl.value = baseline.coralLoad || 'custom';
      if (baseKalkEl) baseKalkEl.value = Number(baseline.kalk || 0).toFixed(0);
      if (baseCacl2El) baseCacl2El.value = Number(baseline.cacl2 || 0).toFixed(0);
      if (baseNaohEl) baseNaohEl.value = Number(baseline.naoh || 0).toFixed(0);
      if (baseMgEl) baseMgEl.value = Number(baseline.mg || 0).toFixed(0);
    }

    const learning = s.alkDemandLearning || {};
    if (!isAlkDemandLearningEditing()) {
      const enabledEl = document.getElementById('alkDemandLearningEnabled');
      if (enabledEl) enabledEl.checked = !!learning.enabled;
    }

    const daysCollected = Number(learning.daysCollected || 0);
    let learningState = 'Recommendation Only';
    if (learning.enabled && learning.ready) learningState = 'Learning Active';
    else if (learning.enabled) learningState = `Collecting (${daysCollected}/7 days)`;

    const learningPill = document.getElementById('alkDemandLearningPill');
    if (learningPill) learningPill.textContent = learningState;

    const stateEl = document.getElementById('alkDemandState');
    if (stateEl) stateEl.value = learningState;

    const daysEl = document.getElementById('alkDemandDays');
    if (daysEl) daysEl.value = `${daysCollected} of 7 days${learning.ready ? ' • ready' : ''}`;

    const curP4 = document.getElementById('alkDemandCurrentP4');
    const recP4 = document.getElementById('alkDemandRecommendedP4');
    const recDkh = document.getElementById('alkDemandRecommendedDkh');
    if (curP4) curP4.textContent = `${safeNum(learning.currentP4MlDay, 1)} mL/day`;
    if (recP4) recP4.textContent = `${safeNum(learning.recommendedP4MlDay, 1)} mL/day`;
    if (recDkh) recDkh.textContent = `${safeNum(learning.recommendedDemandDkhDay, 3)} dKH/day`;

    const caLearning = s.calciumDemandLearning || {};
    if (!isCalciumDemandLearningEditing()) {
      const enabledEl = document.getElementById('calciumDemandLearningEnabled');
      if (enabledEl) enabledEl.checked = !!caLearning.enabled;
    }
    const caDays = Number(caLearning.daysCollected || 0);
    let caState = 'Recommendation Only';
    if (caLearning.enabled && caLearning.ready) caState = 'Learning Active';
    else if (caLearning.enabled) caState = `Collecting (${caDays}/7 days)`;
    const caPill = document.getElementById('calciumDemandLearningPill');
    if (caPill) caPill.textContent = caState;
    const caStateEl = document.getElementById('calciumDemandState');
    if (caStateEl) caStateEl.value = caState;
    const caDaysEl = document.getElementById('calciumDemandDays');
    if (caDaysEl) caDaysEl.value = `${caDays} of 7 days${caLearning.ready ? ' • ready' : ''}`;
    const curP2 = document.getElementById('calciumDemandCurrentP2');
    const recP2 = document.getElementById('calciumDemandRecommendedP2');
    const recPpm = document.getElementById('calciumDemandRecommendedPpm');
    if (curP2) curP2.textContent = `${safeNum(caLearning.currentP2MlDay, 1)} mL/day`;
    if (recP2) recP2.textContent = `${safeNum(caLearning.recommendedP2MlDay, 1)} mL/day`;
    if (recPpm) recPpm.textContent = `${safeNum(caLearning.recommendedDemandPpmDay, 3)} ppm/day`;

    const strengths = s.chemicalStrengths || {};
    if (!isChemicalStrengthEditing()) {
      const strKalkEl = document.getElementById('strKalk');
      const strAfrEl = document.getElementById('strAfr');
      const strAlkEl = document.getElementById('strAlk');
      const strNaohEl = document.getElementById('strNaoh');
      const strMgEl = document.getElementById('strMg');
      const strCacl2El = document.getElementById('strCacl2');
      if (strKalkEl) strKalkEl.value = Number(strengths.kalk || 0).toFixed(7);
      if (strAfrEl) strAfrEl.value = Number(strengths.afr || 0).toFixed(7);
      if (strAlkEl) strAlkEl.value = Number(strengths.alk || 0).toFixed(7);
      if (strNaohEl) strNaohEl.value = Number(strengths.naoh || 0).toFixed(7);
      if (strMgEl) strMgEl.value = Number(strengths.mg || 0).toFixed(5);
      if (strCacl2El) strCacl2El.value = Number(strengths.cacl2 || 0).toFixed(5);

      const recipe = s.chemicalRecipes || {};
      const setRecipeVal = (id, val) => {
        const input = document.getElementById(id);
        if (!input || document.activeElement === input) return;
        const shown = getRecipeUnitMode() === 'liter' ? Number(val) / 3.78541 : Number(val);
        input.value = formatRecipeValue(shown);
      };

      const afrTypeEl = document.getElementById('recipeAfrType');
      if (afrTypeEl && document.activeElement !== afrTypeEl) afrTypeEl.value = recipe.afrType || 'tm_afr_powder';

      setRecipeVal('recipeKalkGpg', Number(recipe.kalkGpg ?? 12));
      setRecipeVal('recipeAlkGpg', Number(recipe.alkGpg ?? 100));
      setRecipeVal('recipeNaohGpg', Number(recipe.naohGpg ?? 144));
      setRecipeVal('recipeMgGpg', Number(recipe.mgGpg ?? 500));
      setRecipeVal('recipeCacl2Gpg', Number(recipe.cacl2Gpg ?? 250));

      const alkTypeEl = document.getElementById('recipeAlkType');
      const mgTypeEl = document.getElementById('recipeMgType');
      const caTypeEl = document.getElementById('recipeCacl2Type');
      if (alkTypeEl && document.activeElement !== alkTypeEl) alkTypeEl.value = recipe.alkType || 'soda_ash';
      if (mgTypeEl && document.activeElement !== mgTypeEl) mgTypeEl.value = recipe.mgType || 'mag_chloride';
      if (caTypeEl && document.activeElement !== caTypeEl) caTypeEl.value = recipe.cacl2Type || 'cacl2_dihydrate';

      updateRecipePreview();
    }

    const tankInput = document.getElementById('tankGal');
    if (tankInput && document.activeElement !== tankInput) {
      const gal = Number(s.tankGallons ?? s.tankGal ?? s.gallons);
      if (Number.isFinite(gal) && gal > 0) tankInput.value = gal.toFixed(1);
    }
    if (typeof updateRecipePreview === 'function') updateRecipePreview();
    }

  // Populate the hour dropdowns (0-23)
function populateHours() {
    const start = document.getElementById('lStart');
    const end = document.getElementById('lEnd');
    if (!start || start.options.length > 0) return; // Already populated

    for (let i = 0; i < 24; i++) {
        let label = i === 0 ? "12 AM" : i === 12 ? "12 PM" : i > 12 ? (i - 12) + " PM" : i + " AM";
        start.options.add(new Option(label, i));
        end.options.add(new Option(label, i));
    }
    // Set defaults (8 AM to 8 PM)
    start.value = 8;
    end.value = 20;
}
    function gv(id) {
  const el = document.getElementById(id);
  return el ? el.value : "";
}



  // Restored 2026-08-04. Sends only these 3 fields -- handlePostAiChemistrySafeties
  // in WebRoutes.cpp already falls back to the current stored value for any
  // field not present in the request body (confirmed by reading that handler
  // directly), so a partial payload is safe and does not disturb the 5
  // genuinely-dead fields' stored values.
  async function saveAiRiseSafeties(){
    const errEl = null; // no dedicated error slot on this small card; use alert()
    const alkRise = parseFloat(document.getElementById('aiSafeMaxAlkRise').value);
    const caRise = parseFloat(document.getElementById('aiSafeMaxCaRise').value);
    const mgCorr = parseFloat(document.getElementById('aiSafeMaxMgCorrection').value);

    if (!Number.isFinite(alkRise) || alkRise <= 0) { alert('Enter a valid Max Alk Rise (dKH/day) greater than 0.'); return; }
    if (!Number.isFinite(caRise) || caRise <= 0) { alert('Enter a valid Max Ca Rise (ppm/day) greater than 0.'); return; }
    if (!Number.isFinite(mgCorr) || mgCorr < 0) { alert('Enter a valid Max Mg Correction (mL/day), 0 or more.'); return; }

    const res = await api('/api/config/ai-chemistry-safeties', 'POST', {
      maxAlkRiseDkhDay: alkRise,
      maxCaRisePpmDay: caRise,
      maxMgCorrectionDayMl: mgCorr
    });
    if (res && res.ok === false) {
      alert('Save failed: ' + (res.error || res.raw || 'unknown error'));
      return;
    }
    await loadAll();
    alert('Dosing safety rise limits updated.');
  }

  async function loadAll(){
    
    try{
      const s = await api('/api/status');
      renderStatus(s);
      addRealtimePoint(s);
      loadDosingHistory(false);
    }catch(err){
      document.getElementById('syncBox').textContent = 'OFFLINE';
      console.error(err);
    }
  }

  async function setMode(mode){
    await api('/api/mode', 'POST', { mode });
    await loadAll();
  }

  function renderManageChemicals(){
    const wrap = document.getElementById('chemManageList');
    const pill = document.getElementById('chemCountPill');
    if (!wrap) return;
    if (pill) pill.textContent = declaredChemicalsCache.length + ' declared';

    const sorted = declaredChemicalsCache.slice().sort((a,b) => a.pumpIndex - b.pumpIndex);
    wrap.innerHTML = sorted.map(c => {
      const presetMatch = chemicalPresetsCache.find(p => p.id === c.presetId);
      const presetName = c.presetId >= 0 && presetMatch ? presetMatch.name : 'Custom';
      const potencyParts = [];
      if (c.potencyAlkPerMl) potencyParts.push(c.potencyAlkPerMl.toFixed(5) + ' dKH/mL');
      if (c.potencyCaPerMl) potencyParts.push(c.potencyCaPerMl.toFixed(5) + ' ppm Ca/mL');
      if (c.potencyMgPerMl) potencyParts.push(c.potencyMgPerMl.toFixed(5) + ' ppm Mg/mL');
      const safeName = String(c.name).replace(/'/g, "\\'");
      const lowerName = String(c.name).toLowerCase();
      const isKalk = lowerName.indexOf('kalk') >= 0;

      // Added 2026-08-04: there was NO way to edit an already-declared
      // chemical's potency anywhere in the dashboard until now -- only the
      // one-time setup wizard could set it, and the newer per-chemical
      // fields added earlier tonight (Daily Dose Cap etc.) never covered
      // this. Confirmed as a real, direct gap: fixing a wrong potency
      // (like a real 3,000x error found on a live customer device) had no
      // path except a raw API call from the browser console. Three cases,
      // matching the backend guards added alongside this:
      //   1. Kalk-named + custom (presetId<0): editing is now BLOCKED
      //      server-side (see handlePostUpdateChemical) -- kalkwasser is a
      //      saturated solution with one real concentration, not a
      //      customer recipe. Show a clear explanation + the fix (switch
      //      to the Kalkwasser preset) instead of input boxes that would
      //      just fail on save.
      //   2. Any other custom (presetId<0): real, editable potency fields.
      //   3. Preset-owned (presetId>=0): potency is preset-derived, not
      //      directly editable -- matches presetToDeclaration()'s existing
      //      "preset owns these fields" model. Switching products (the
      //      existing presetId control elsewhere on this card) is how you
      //      change potency for these.
      let potencyEditHtml = '';
      if (isKalk && c.presetId < 0) {
        // Fixed 2026-08-04: the original version of this message told
        // people to Remove and re-add via the preset -- confirmed as a
        // real dead end: Remove is correctly blocked by the existing
        // §5.2 sufficiency check whenever this chemical is still
        // contributing meaningfully toward a target (which a wrong-but-
        // nonzero potency, like the exact bug this whole guard exists to
        // catch, always will be). That left NO path forward for fixing an
        // already-wrong Kalk entry at all. Switching presetId on the
        // EXISTING chemical in one atomic update (already supported by
        // handlePostUpdateChemical) never creates a moment where Kalk
        // contributes zero, so the sufficiency check has nothing to
        // object to -- no remove step needed at all.
        potencyEditHtml = `
          <div class="help" style="margin-top:10px; color:var(--danger);">
            <b>This chemical's potency can't be edited as a custom number.</b>
            Kalkwasser is a saturated solution -- there is one real concentration,
            not a customer recipe.
          </div>
          <button class="sec" style="margin-top:8px" onclick="switchToKalkwasserPreset('${c.id}')">Switch to Kalkwasser Preset</button>
          <div class="help" style="margin-top:4px">Recalculates this chemical's real potency from actual chemistry, using this tank's current volume -- no need to remove and re-add it.</div>
          <div class="help" style="margin-top:4px" id="potencyError_${c.id}"></div>`;
      } else if (c.presetId < 0) {
        // Added 2026-08-04, at owner's direct request: a raw dKH/mL number
        // like 0.0000046 is exactly as unverifiable by eye as the wrong
        // 0.014 that caused a real 3,000x error on a live device -- a
        // human can't sanity-check either one. Reuses the SAME
        // grams-per-gallon recipe math the setup wizard already has for
        // NaOH/baking soda/CaCl2 (see WIZARD_DIY_RECIPES), matched by
        // chemical name since an already-declared custom chemical's
        // presetId is always -1 regardless of which path originally
        // created it. Falls back to raw entry only for a genuinely
        // unrecognized custom chemical -- there's no way around needing a
        // real number for a chemical this system doesn't have known
        // chemistry for at all.
        const recipeMatch = findRecipeMatchByName(c.name);
        if (recipeMatch) {
          const currentGrams = (c.potencyAlkPerMl || c.potencyCaPerMl)
            ? estimateGramsFromPotency(c, recipeMatch)
            : recipeMatch.recipeInfo.standardGrams;
          potencyEditHtml = `
            <div class="line" style="margin-top:10px">
              <div class="k">Recipe: grams per gallon</div>
              <input type="number" step="0.1" value="${currentGrams.toFixed(1)}" id="potGrams_${c.id}" style="width:90px">
              <button class="sec" style="margin-left:8px" onclick="savePotencyFromRecipe('${c.id}', ${recipeMatch.presetId})">Save</button>
            </div>
            <div class="help" style="margin-top:4px">${recipeMatch.recipeInfo.label} standard recipe: ${recipeMatch.recipeInfo.standardGrams} g/gal. Enter what THIS tank's mix actually uses -- the real per-mL potency is calculated from this automatically, same as the setup wizard.</div>
            <div class="help" style="margin-top:4px" id="potencyError_${c.id}"></div>`;
        } else {
          potencyEditHtml = `
            <div class="help" style="margin-top:10px">No known recipe for "${c.name}" -- enter its real potency directly (get this from testing or the product's own documentation, not a guess).</div>
            <div class="line" style="margin-top:10px">
              <div class="k">Alk Potency (this chemical)</div>
              <input type="number" step="0.00001" value="${c.potencyAlkPerMl}" id="potAlk_${c.id}" style="width:110px"> dKH/mL
            </div>
            <div class="line" style="margin-top:6px">
              <div class="k">Ca Potency (this chemical)</div>
              <input type="number" step="0.00001" value="${c.potencyCaPerMl}" id="potCa_${c.id}" style="width:110px"> ppm/mL
            </div>
            <div class="line" style="margin-top:6px">
              <div class="k">Mg Potency (this chemical)</div>
              <input type="number" step="0.00001" value="${c.potencyMgPerMl}" id="potMg_${c.id}" style="width:110px"> ppm/mL
            </div>
            <button class="sec" style="margin-top:8px" onclick="savePotency('${c.id}')">Save Potency</button>
            <div class="help" style="margin-top:4px" id="potencyError_${c.id}"></div>`;
        }
      } else {
        // Fixed 2026-08-04: the old text here referenced "the preset
        // selector" to change potency, but no such live control actually
        // existed for an already-preset-based chemical -- confirmed
        // directly, Kalkwasser's real, already-declared potency stayed
        // frozen at 0.0 for pH even after the underlying preset data
        // gained a real value, because potency is only ever computed
        // and saved at the MOMENT a preset is applied, never
        // recalculated live from whatever's currently in
        // ChemicalPresets.cpp. This button actually closes that gap --
        // resubmits the SAME presetId this chemical already has, which
        // handlePostUpdateChemical unconditionally recomputes fresh from
        // presetToDeclaration() every time (no "unchanged, skip" check
        // exists there, confirmed directly).
        potencyEditHtml = `
          <div class="help" style="margin-top:10px">Potency comes from the ${presetName} preset.</div>
          <button class="sec" style="margin-top:8px" onclick="refreshFromPreset('${c.id}', ${c.presetId})">Refresh from Preset</button>
          <div class="help" style="margin-top:4px">Recalculates this chemical's potency from the preset's current chemistry data and this tank's current volume -- use this if the preset's known chemistry has been updated/corrected since this chemical was first added, or after changing tank volume.</div>
          <div class="help" style="margin-top:4px" id="potencyError_${c.id}"></div>`;
      }

      return `
        <div class="cal-item">
          <div class="cal-top">
            <div>
              <div class="cal-name">${c.name}${c.phSensitive ? ' \u{1F9EA}' : ''}</div>
              <div class="cal-sub">Pump ${c.pumpIndex + 1} \u2022 ${presetName} \u2022 ${potencyParts.join(', ') || 'no potency set'}</div>
            </div>
            <div class="chem-badge${c.active ? '' : ' warn'}">${c.active ? 'Active' : 'Disabled'}</div>
          </div>
          ${potencyEditHtml}
          <div class="line" style="margin-top:10px">
            <div class="k">Night Dosing %</div>
            <input type="number" min="0" max="100" step="1" value="${c.nightFraction}" id="nightFrac_${c.id}" style="width:70px">
            <button class="sec" style="margin-left:8px" onclick="saveNightFraction('${c.id}')">Save</button>
          </div>
          <div class="help" style="margin-top:4px">% of this chemical's daily total dispensed at night. 50 = even all day, 100 = night only, 0 = day only.</div>
          <!-- Added 2026-08-02, corrected 2026-08-04: this chemical's own
               daily dose cap was previously only settable once, at creation
               time. That part is now fixed by this field existing at all.
               UPDATE 2026-08-04: the comment here originally said this and
               "Pump Safeties" were two permanently separate numbers -- that
               was itself an incomplete understanding. They WERE separate at
               the physical-execution layer too (not just planning), which
               was a second, deeper instance of the same bug: editing this
               value alone didn't used to guarantee the physical pump would
               actually honor it. Fixed in main.cpp's getPumpMaxDayMl() --
               this chemical's maxMlPerDay now takes priority at BOTH the
               allocator-planning layer AND the physical-dispensing safety
               layer. Pump Safeties' per-pump value is now only a fallback,
               used only if a pump has no chemical declared on it yet. -->
          <div class="line" style="margin-top:10px">
            <div class="k">Daily Dose Cap (this chemical)</div>
            <input type="number" min="0" step="1" value="${c.maxMlPerDay}" id="maxMlDay_${c.id}" style="width:90px"> mL/day
            <button class="sec" style="margin-left:8px" onclick="saveMaxMlPerDay('${c.id}')">Save</button>
          </div>
          <div class="help" style="margin-top:4px">Hard daily ceiling for THIS chemical -- enforced both by the dosing allocator's planning AND by the physical pump before it fires. If this is set too low, the allocator may be unable to fully correct a parameter even when it has plenty of room left in Pump Safeties.</div>

          <!-- Added 2026-08-04: same reasoning as Daily Dose Cap above,
               closing the same gap for the other two per-chemical safety
               values that used to live pump-slot-only. -->
          <div class="line" style="margin-top:10px">
            <div class="k">Bucket Threshold (this chemical)</div>
            <input type="number" min="0.01" step="0.01" value="${c.bucketThresholdMl}" id="bucketThresh_${c.id}" style="width:90px"> mL
            <button class="sec" style="margin-left:8px" onclick="saveBucketThreshold('${c.id}')">Save</button>
          </div>
          <div class="help" style="margin-top:4px">How much of this chemical must accumulate before its pump actually fires a dose.</div>
          <div class="line" style="margin-top:10px">
            <div class="k">Max Single Dose (this chemical)</div>
            <input type="number" min="0.01" step="0.01" value="${c.maxSingleDoseMl}" id="maxSingleDose_${c.id}" style="width:90px"> mL
            <button class="sec" style="margin-left:8px" onclick="saveMaxSingleDose('${c.id}')">Save</button>
          </div>
          <div class="help" style="margin-top:4px">Cap for one physical pump run of this chemical, before the 60-second hardware limit.</div>
          ${c.phSensitive ? `
          <div class="line" style="margin-top:10px">
            <div class="k">Avoid During Day %</div>
            <input type="number" min="0" max="100" step="1" value="${c.daytimeSuppressPercent}" id="daySuppress_${c.id}" style="width:70px">
            <button class="sec" style="margin-left:8px" onclick="saveDaytimeSuppress('${c.id}')">Save</button>
          </div>
          <div class="help" style="margin-top:4px">How strongly to avoid using this chemical during lights-on hours, independent of pH. 0 = no avoidance, 100 = don't use it at all during the day (another declared chemical for the same parameter, if any, is used instead).</div>
          ` : ''}
          <div class="three" style="margin-top:10px">
            <button class="sec" onclick="toggleChemicalActive('${c.id}', ${!c.active})">${c.active ? 'Disable' : 'Enable'}</button>
            <button class="danger" onclick="removeChemicalConfirm('${c.id}', '${safeName}')">Remove</button>
          </div>
          <div class="help" id="chemError_${c.id}" style="color:var(--danger);margin-top:8px"></div>
        </div>
      `;
    }).join('');
  }

  async function saveNightFraction(id){
    const errEl = document.getElementById('chemError_' + id);
    const input = document.getElementById('nightFrac_' + id);
    const nf = parseFloat(input.value);
    if (!Number.isFinite(nf) || nf < 0 || nf > 100) {
      if (errEl) errEl.textContent = 'Enter a value between 0 and 100.';
      return;
    }
    try {
      const result = await api('/api/chemicals/update', 'POST', { id, nightFraction: nf });
      if (!result.ok) {
        if (errEl) errEl.textContent = result.error || 'Failed to save.';
        showToast('Could not save: ' + (result.error || 'unknown error'), true);
        return;
      }
    } catch (e) {
      if (errEl) errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    if (errEl) errEl.textContent = '';
    showToast('Saved.', false);
    await loadDeclaredChemicals();
  }

  // Added 2026-08-02, see the "Daily Dose Cap (this chemical)" field's inline
  // comment above renderManageChemicals() for the full incident this closes.
  // No upper bound check (unlike the 0-100 percent fields above) -- this is a
  // real mL/day volume, whatever ceiling makes sense depends entirely on the
  // chemical/tank, same reasoning as the existing Pump Safeties max/day field.
  async function saveMaxMlPerDay(id){
    const errEl = document.getElementById('chemError_' + id);
    const input = document.getElementById('maxMlDay_' + id);
    const cap = parseFloat(input.value);
    if (!Number.isFinite(cap) || cap < 0) {
      if (errEl) errEl.textContent = 'Enter a value of 0 or more.';
      return;
    }
    try {
      const result = await api('/api/chemicals/update', 'POST', { id, maxMlPerDay: cap });
      if (!result.ok) {
        if (errEl) errEl.textContent = result.error || 'Failed to save.';
        showToast('Could not save: ' + (result.error || 'unknown error'), true);
        return;
      }
    } catch (e) {
      if (errEl) errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    if (errEl) errEl.textContent = '';
    showToast('Saved.', false);
    await loadDeclaredChemicals();
  }

  // Added 2026-08-04: same pattern as saveMaxMlPerDay above, for the two
  // fields that used to live pump-slot-only. Both require > 0 (unlike
  // maxMlPerDay's >= 0) since a zero threshold or zero max-single-dose
  // would mean the pump can never actually fire.
  async function saveBucketThreshold(id){
    const errEl = document.getElementById('chemError_' + id);
    const input = document.getElementById('bucketThresh_' + id);
    const val = parseFloat(input.value);
    if (!Number.isFinite(val) || val <= 0) {
      if (errEl) errEl.textContent = 'Enter a value greater than 0.';
      return;
    }
    try {
      const result = await api('/api/chemicals/update', 'POST', { id, bucketThresholdMl: val });
      if (!result.ok) {
        if (errEl) errEl.textContent = result.error || 'Failed to save.';
        showToast('Could not save: ' + (result.error || 'unknown error'), true);
        return;
      }
    } catch (e) {
      if (errEl) errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    if (errEl) errEl.textContent = '';
    showToast('Saved.', false);
    await loadDeclaredChemicals();
  }

  async function saveMaxSingleDose(id){
    const errEl = document.getElementById('chemError_' + id);
    const input = document.getElementById('maxSingleDose_' + id);
    const val = parseFloat(input.value);
    if (!Number.isFinite(val) || val <= 0) {
      if (errEl) errEl.textContent = 'Enter a value greater than 0.';
      return;
    }
    try {
      const result = await api('/api/chemicals/update', 'POST', { id, maxSingleDoseMl: val });
      if (!result.ok) {
        if (errEl) errEl.textContent = result.error || 'Failed to save.';
        showToast('Could not save: ' + (result.error || 'unknown error'), true);
        return;
      }
    } catch (e) {
      if (errEl) errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    if (errEl) errEl.textContent = '';
    showToast('Saved.', false);
    await loadDeclaredChemicals();
  }

  // Added 2026-08-04: the only way, until now, to fix an already-declared
  // chemical's potency was a raw API call from the browser console --
  // confirmed as a real gap directly, not assumed. Handles the backend's
  // new "this looks like a generic placeholder, not a real measurement"
  // guard (see handlePostUpdateChemical) -- if it fires, asks the person
  // directly whether the value is genuinely correct before resubmitting
  // with confirmGenericValue, rather than silently overriding a real
  // safety check on the client side.
  async function savePotency(id){
    const errEl = document.getElementById('potencyError_' + id);
    const alkVal = parseFloat(document.getElementById('potAlk_' + id).value);
    const caVal  = parseFloat(document.getElementById('potCa_' + id).value);
    const mgVal  = parseFloat(document.getElementById('potMg_' + id).value);

    if (![alkVal, caVal, mgVal].every(Number.isFinite)) {
      if (errEl) errEl.textContent = 'Enter a valid number (0 is fine) for each field.';
      return;
    }
    if (alkVal === 0 && caVal === 0 && mgVal === 0) {
      if (errEl) errEl.textContent = 'At least one of Alk/Ca/Mg potency must be nonzero.';
      return;
    }

    const body = { id, potencyAlkPerMl: alkVal, potencyCaPerMl: caVal, potencyMgPerMl: mgVal };

    try {
      let result = await api('/api/chemicals/update', 'POST', body);
      if (!result.ok && (result.error || '').indexOf('generic placeholder') >= 0) {
        const reallySure = confirm(
          (result.error || '') +
          '\n\nClick OK only if you have actually confirmed this tank\'s real mixed strength ' +
          'matches this number -- not because it looked like a reasonable default.'
        );
        if (!reallySure) {
          if (errEl) errEl.textContent = 'Not saved -- enter this tank\'s actual measured strength.';
          return;
        }
        body.confirmGenericValue = true;
        result = await api('/api/chemicals/update', 'POST', body);
      }
      if (!result.ok) {
        if (errEl) errEl.textContent = result.error || 'Failed to save.';
        showToast('Could not save potency: ' + (result.error || 'unknown error'), true);
        return;
      }
    } catch (e) {
      if (errEl) errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    if (errEl) errEl.textContent = '';
    showToast('Potency saved.', false);
    await loadDeclaredChemicals();
  }

  // Added 2026-08-04: same grams-per-gallon-to-potency math the wizard's
  // recipeInfo branch already uses (see wizardSubmitNewChemical), applied
  // here for EDITING an already-declared chemical instead of only at
  // initial creation. Computes a real potencyAlkPerMl/potencyCaPerMl from
  // a human-meaningful number before submitting -- the person never has
  // to type or verify a raw dKH/mL value by eye.
  // Added 2026-08-04: closes the confirmed gap where an already-preset-
  // based chemical had no way to pick up improved preset chemistry data
  // (e.g. Kalkwasser's pH potency staying frozen at 0.0 even after real
  // values were added to ChemicalPresets.cpp, since potency is only ever
  // computed at the moment a preset is applied, never live-recalculated).
  // Resubmits the same presetId this chemical already has -- the backend
  // recomputes unconditionally from current data every time, confirmed
  // directly in handlePostUpdateChemical.
  async function refreshFromPreset(id, presetId){
    const errEl = document.getElementById('potencyError_' + id);
    try {
      const result = await api('/api/chemicals/update', 'POST', { id, presetId });
      if (!result.ok) {
        if (errEl) errEl.textContent = result.error || 'Failed to refresh.';
        showToast('Could not refresh from preset: ' + (result.error || 'unknown error'), true);
        return;
      }
    } catch (e) {
      if (errEl) errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    if (errEl) errEl.textContent = '';
    showToast('Potency refreshed from preset.', false);
    await loadDeclaredChemicals();
  }

  async function savePotencyFromRecipe(id, presetId){
    const errEl = document.getElementById('potencyError_' + id);
    const grams = parseFloat(document.getElementById('potGrams_' + id).value);
    if (!Number.isFinite(grams) || grams <= 0) {
      if (errEl) errEl.textContent = 'Enter the grams per gallon this tank actually mixes.';
      return;
    }
    const tankGal = Number(currentStatus.tankGallons) || 0;
    if (!(tankGal > 0)) {
      if (errEl) errEl.textContent = 'Tank volume isn\u2019t set -- set it under System Geometry first.';
      return;
    }
    const recipeInfo = WIZARD_DIY_RECIPES[presetId];
    const preset = chemicalPresetsCache.find(p => p.id === presetId);
    if (!recipeInfo || !preset) {
      if (errEl) errEl.textContent = 'Recipe data not loaded -- refresh the page and try again.';
      return;
    }

    const scale = grams / recipeInfo.standardGrams;
    const body = {
      id,
      potencyAlkPerMl: (preset.potencyAlkPerGallon || 0) * scale / tankGal,
      potencyCaPerMl:  (preset.potencyCaPerGallon  || 0) * scale / tankGal,
      potencyMgPerMl:  (preset.potencyMgPerGallon  || 0) * scale / tankGal,
      // Added 2026-08-04: was missing entirely -- confirmed NaOH and the
      // new soda-ash preset both had real pH values in ChemicalPresets.cpp
      // that this path never transferred, since neither this field nor
      // the backend endpoint it reads from included pH at all until now.
      potencyPhPerMl:  (preset.potencyPhPerGallon  || 0) * scale / tankGal
    };

    try {
      let result = await api('/api/chemicals/update', 'POST', body);
      if (!result.ok && (result.error || '').indexOf('generic placeholder') >= 0) {
        const reallySure = confirm(
          (result.error || '') +
          '\n\nClick OK only if this genuinely matches this tank\'s real recipe.'
        );
        if (!reallySure) {
          if (errEl) errEl.textContent = 'Not saved.';
          return;
        }
        body.confirmGenericValue = true;
        result = await api('/api/chemicals/update', 'POST', body);
      }
      if (!result.ok) {
        if (errEl) errEl.textContent = result.error || 'Failed to save.';
        showToast('Could not save potency: ' + (result.error || 'unknown error'), true);
        return;
      }
    } catch (e) {
      if (errEl) errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    if (errEl) errEl.textContent = '';
    showToast('Potency saved.', false);
    await loadDeclaredChemicals();
  }

  // Added 2026-08-04: the actual fix for an already-wrong Kalk entry --
  // see the button's own comment in the render function above for the
  // full reasoning (Remove is correctly blocked by the sufficiency check,
  // direct potency editing is correctly blocked too, so switching preset
  // in place is the only path that never creates a moment with zero Kalk
  // contribution). Looks up the real preset by NAME from the live-loaded
  // chemicalPresetsCache rather than hardcoding its numeric id -- safer
  // against the backend's preset enum ever being reordered/extended.
  async function switchToKalkwasserPreset(id){
    const errEl = document.getElementById('potencyError_' + id);
    const kalkPreset = chemicalPresetsCache.find(p => String(p.name).toLowerCase().indexOf('kalkwasser') >= 0);
    if (!kalkPreset) {
      if (errEl) errEl.textContent = 'Kalkwasser preset not found -- refresh the page and try again.';
      return;
    }
    try {
      const result = await api('/api/chemicals/update', 'POST', { id, presetId: kalkPreset.id });
      if (!result.ok) {
        if (errEl) errEl.textContent = result.error || 'Failed to switch preset.';
        showToast('Could not switch to Kalkwasser preset: ' + (result.error || 'unknown error'), true);
        return;
      }
    } catch (e) {
      if (errEl) errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    if (errEl) errEl.textContent = '';
    showToast('Switched to Kalkwasser preset -- potency recalculated from real chemistry.', false);
    await loadDeclaredChemicals();
  }

  async function saveDaytimeSuppress(id){
    const errEl = document.getElementById('chemError_' + id);
    const input = document.getElementById('daySuppress_' + id);
    const dsp = parseFloat(input.value);
    if (!Number.isFinite(dsp) || dsp < 0 || dsp > 100) {
      if (errEl) errEl.textContent = 'Enter a value between 0 and 100.';
      return;
    }
    try {
      const result = await api('/api/chemicals/update', 'POST', { id, daytimeSuppressPercent: dsp });
      if (!result.ok) {
        if (errEl) errEl.textContent = result.error || 'Failed to save.';
        showToast('Could not save: ' + (result.error || 'unknown error'), true);
        return;
      }
    } catch (e) {
      if (errEl) errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    if (errEl) errEl.textContent = '';
    showToast('Saved.', false);
    await loadDeclaredChemicals();
  }

  function populatePresetDropdown(){
    const sel = document.getElementById('newChemPreset');
    if (!sel) return;
    const custom = '<option value="-1">Custom (enter potency manually)</option>';
    sel.innerHTML = custom + chemicalPresetsCache.map(p => {
      const tags = [p.movesAlk ? 'Alk' : null, p.movesCa ? 'Ca' : null, p.movesMg ? 'Mg' : null].filter(Boolean).join('+');
      return `<option value="${p.id}">${p.name} (${tags}${p.phSensitive ? ', raises pH' : ''})</option>`;
    }).join('');
  }

  // Owner decision 2026-07-24: always require a pump at creation, and one
  // physical pump can only run one chemical -- so the add form only ever
  // offers pumps nothing else is already using.
  function freePumpIndexes(){
    const used = new Set(declaredChemicalsCache.map(c => c.pumpIndex));
    const free = [];
    for (let i = 0; i < 4; i++) if (!used.has(i)) free.push(i);
    return free;
  }

  function populateNewChemPumpSelect(){
    const sel = document.getElementById('newChemPump');
    if (!sel) return;
    sel.innerHTML = freePumpIndexes().map(i => `<option value="${i}">Pump ${i + 1}</option>`).join('');
  }

  function showAddChemicalForm(){
    if (freePumpIndexes().length === 0) {
      alert('All 4 physical pumps already have a chemical assigned. Remove one first.');
      return;
    }
    document.getElementById('addChemicalForm').style.display = 'block';
    document.getElementById('addChemicalError').textContent = '';
    populateNewChemPumpSelect();
    onNewChemPresetChange();
  }

  function hideAddChemicalForm(){
    document.getElementById('addChemicalForm').style.display = 'none';
    document.getElementById('newChemName').value = '';
    document.getElementById('newChemAlk').value = '';
    document.getElementById('newChemCa').value = '';
    document.getElementById('newChemMg').value = '';
    document.getElementById('newChemPhSensitive').checked = false;
  }

  function onNewChemPresetChange(){
    const presetId = parseInt(document.getElementById('newChemPreset').value, 10);
    const customFields = document.getElementById('newChemCustomFields');
    if (customFields) customFields.style.display = presetId < 0 ? 'block' : 'none';
  }

  async function submitAddChemical(){
    const errEl = document.getElementById('addChemicalError');
    errEl.textContent = '';

    const name = document.getElementById('newChemName').value.trim();
    const presetId = parseInt(document.getElementById('newChemPreset').value, 10);
    const pumpIndex = parseInt(document.getElementById('newChemPump').value, 10);

    if (!name) { errEl.textContent = 'Name is required.'; return; }
    if (!Number.isFinite(pumpIndex)) { errEl.textContent = 'No free pump available.'; return; }

    const body = { name, pumpIndex, presetId };
    if (presetId < 0) {
      body.potencyAlkPerMl = parseFloat(document.getElementById('newChemAlk').value) || 0;
      body.potencyCaPerMl = parseFloat(document.getElementById('newChemCa').value) || 0;
      body.potencyMgPerMl = parseFloat(document.getElementById('newChemMg').value) || 0;
      body.phSensitive = document.getElementById('newChemPhSensitive').checked;
    }

    let result = await api('/api/chemicals', 'POST', body);
    // Added 2026-08-04: same confirm-and-resubmit flow as savePotency/
    // savePotencyFromRecipe on the edit side -- the "generic placeholder"
    // guard (handlePostAddChemical) has always fired here too, but until
    // now this form only surfaced the raw error with no way for someone
    // whose real value genuinely matches a generic default to proceed.
    if (!result.ok && (result.error || '').indexOf('generic placeholder') >= 0) {
      const reallySure = confirm(
        (result.error || '') +
        '\n\nClick OK only if you have actually confirmed this tank\'s real mixed strength ' +
        'matches this number -- not because it looked like a reasonable default.'
      );
      if (!reallySure) {
        errEl.textContent = 'Not added -- enter this tank\'s actual measured strength.';
        return;
      }
      body.confirmGenericValue = true;
      result = await api('/api/chemicals', 'POST', body);
    }
    if (!result.ok) {
      errEl.textContent = result.error || 'Failed to add chemical.';
      showToast('Could not add ' + name + ': ' + (result.error || 'unknown error'), true);
      return;
    }

    showToast(name + ' added.', false);
    hideAddChemicalForm();
    await loadDeclaredChemicals();
  }

  async function toggleChemicalActive(id, newActive){
    const result = await api('/api/chemicals/update', 'POST', { id, active: newActive });
    if (!result.ok) {
      alert(result.error || 'Failed to update chemical.');
      return;
    }
    await loadDeclaredChemicals();
  }

  async function removeChemicalConfirm(id, name){
    if (!confirm('Remove ' + name + '? This cannot be undone.')) return;
    const result = await api('/api/chemicals/remove', 'POST', { id });
    if (!result.ok) {
      // §5.2 hard block surfaces here -- e.g. "would leave insufficient
      // capacity for your current targets." Shown inline on the specific
      // chemical's row rather than a generic alert, so it's clear which
      // removal was rejected and why.
      const errEl = document.getElementById('chemError_' + id);
      if (errEl) errEl.textContent = result.error || 'Failed to remove.';
      else alert(result.error || 'Failed to remove.');
      showToast('Could not remove ' + name + ': ' + (result.error || 'unknown error'), true);
      return;
    }
    showToast(name + ' removed.', false);
    await loadDeclaredChemicals();
  }

  // =========================================================================
  // §8/§8.5 One-time Setup Wizard (local dashboard). See overlay HTML
  // comment for the split with the cloud device-setup.html page.
  // =========================================================================
  let wizardCurrentStep = 1;
  const WIZARD_TOTAL_STEPS = 7;

  // Standard recipe reference for DIY chemicals whose concentration
  // genuinely depends on how the customer mixes it -- exactly the case old
  // Mode 7/8 needed customer-supplied strength/formula for. Commercial
  // presets and saturated Kalkwasser need none of this; potency is fixed.
  // Grams figures match the sourcing already documented in
  // ChemicalPresets.cpp (Randy Holmes-Farley's standard recipes).
  const WIZARD_DIY_RECIPES = {
    6: { standardGrams: 283, label: 'Sodium Hydroxide (NaOH)' },
    7: { standardGrams: 297, label: 'Sodium Bicarbonate (baking soda)' },
    9: { standardGrams: 250, label: 'Calcium Chloride, Anhydrous' },
    10: { standardGrams: 250, label: 'Calcium Chloride Dihydrate' },
    // Added 2026-08-04: closes the confirmed "ALK" chemical gap -- see
    // ChemicalPresets.cpp's own comment on this entry for the full
    // sourced derivation (100 g/gal, matches the AI Doser Standard
    // recipe already referenced elsewhere in this codebase).
    11: { standardGrams: 100, label: 'Alkalinity (Soda Ash)' }
  };

  // Added 2026-08-04: lets Manage Chemicals match an ALREADY-declared
  // chemical back to a known recipe type by NAME, since presetId is
  // always -1 for any custom-path chemical regardless of whether it
  // originally went through the wizard's recipe math or raw entry --
  // presetId alone can't tell these apart after the fact. Checked in
  // order; "calcium chloride" alone (no anhydrous/dihydrate specified)
  // defaults to anhydrous (preset 9) since that's the more common form
  // referenced generically, not a claim that's always correct -- a
  // real ambiguity, flagged here rather than silently guessed at.
  const NAME_KEYWORDS_TO_RECIPE_PRESET_ID = [
    ['sodium hydroxide', 6], ['naoh', 6],
    ['baking soda', 7], ['sodium bicarbonate', 7],
    ['calcium chloride dihydrate', 10], ['cacl2 dihydrate', 10],
    ['calcium chloride anhydrous', 9], ['cacl2 anhydrous', 9],
    ['calcium chloride', 9], ['cacl2', 9],
    // Added 2026-08-04: exact-word keywords alone will never reliably
    // catch every real-world spelling -- confirmed directly, a live
    // device's chemical is genuinely named "Calcuim" (typo), which
    // matches none of the specific keywords above. 'calc' is a safe,
    // narrow-enough substring to catch this and similar variants
    // ("Calcium", "Calc", etc.) without plausibly matching anything
    // unrelated -- checked LAST so a name that's explicit about
    // dihydrate/anhydrous still routes correctly above this fallback.
    ['calc', 9],
    // Added 2026-08-04: same reasoning as 'calc' above, for the "ALK"
    // gap this whole session's investigation started from. Most specific
    // first ('soda ash', 'sodium carbonate', 'alkalinity'), bare 'alk'
    // last as the real-world fallback -- confirmed safe from colliding
    // with Kalkwasser: kalk-named chemicals are already intercepted by
    // the separate isKalk branch in the render function above, before
    // this function is ever called, so 'alk' never has a chance to
    // false-match "Kalkwasser" in practice.
    ['soda ash', 11], ['sodium carbonate', 11], ['alkalinity', 11], ['alk', 11]
  ];

  function findRecipeMatchByName(name){
    const lower = String(name).toLowerCase();
    for (const [keyword, presetId] of NAME_KEYWORDS_TO_RECIPE_PRESET_ID) {
      if (lower.indexOf(keyword) >= 0) {
        const recipeInfo = WIZARD_DIY_RECIPES[presetId];
        const preset = chemicalPresetsCache.find(p => p.id === presetId);
        if (recipeInfo && preset) return { presetId, recipeInfo, preset };
      }
    }
    return null;
  }

  // Reverses presetToDeclaration()'s own scale formula to estimate what
  // grams/gallon value would produce this chemical's CURRENTLY stored
  // potency -- so the edit field shows a real starting point instead of
  // always resetting to the generic standard recipe amount. Approximate
  // by design (floating-point round-trip, and uses whichever of Alk/Ca
  // this recipe type actually has a nonzero per-gallon constant for) --
  // good enough for a starting display value, not asserted as exact.
  function estimateGramsFromPotency(c, recipeMatch){
    const tankGal = Number(currentStatus.tankGallons) || 0;
    if (!(tankGal > 0)) return recipeMatch.recipeInfo.standardGrams;
    const preset = recipeMatch.preset;
    if (preset.potencyAlkPerGallon && c.potencyAlkPerMl) {
      return (c.potencyAlkPerMl * tankGal / preset.potencyAlkPerGallon) * recipeMatch.recipeInfo.standardGrams;
    }
    if (preset.potencyCaPerGallon && c.potencyCaPerMl) {
      return (c.potencyCaPerMl * tankGal / preset.potencyCaPerGallon) * recipeMatch.recipeInfo.standardGrams;
    }
    return recipeMatch.recipeInfo.standardGrams;
  }

  function wizardGoTo(step){
    for (let i = 1; i <= WIZARD_TOTAL_STEPS; i++) {
      const el = document.getElementById('wizardStep' + i);
      if (el) el.style.display = (i === step) ? '' : 'none';
    }
    wizardCurrentStep = step;
    const indicator = document.getElementById('wizardStepIndicator');
    if (indicator) indicator.textContent = 'Step ' + step + ' of ' + WIZARD_TOTAL_STEPS;
    const progressFill = document.getElementById('wizardProgressFill');
    if (progressFill) progressFill.style.width = Math.round((step / WIZARD_TOTAL_STEPS) * 100) + '%';
    if (step === 3) {
      renderWizardChemList();
      wizardPopulatePresetDropdown();
    }
    if (step === 4) {
      renderWizardCalibrationList();
    }
    if (step === 5) {
      const s = currentStatus || {};
      if (Number.isFinite(s.targetAlk)) document.getElementById('wizardTargetAlk').value = s.targetAlk;
      if (Number.isFinite(s.targetCa)) document.getElementById('wizardTargetCa').value = s.targetCa;
      if (Number.isFinite(s.targetMg)) document.getElementById('wizardTargetMg').value = s.targetMg;
      const m7 = (s.mode7DayNightSplit || {});
      if (Number.isFinite(m7.naohMaxPh)) document.getElementById('wizardTargetPh').value = m7.naohMaxPh;
      wizardApplyMgVisibility('wizardTargetMgRow', 'wizardTargetMg', 'wizardNoMgNote');
    }
    if (step === 6) {
      wizardApplyMgVisibility('wizardBaselineMgRow', 'wizardMg', null);
    }
  }

  // Owner decision 2026-07-25: only ask for a Mg target/reading if a
  // declared chemical actually touches Mg -- old Mode 7 never dosed Mg at
  // all, so asking for a Mg target there was meaningless. When hidden, the
  // underlying input is still pre-filled with a plausible seawater value
  // (not left blank/zero) since the backend's validation range for both
  // the manual-test and chemistry-targets endpoints requires SOME value in
  // range regardless -- harmless since nothing acts on it when no chemical
  // can touch Mg anyway (matches the "structural, not a dosing shortfall"
  // note already shown elsewhere for this exact situation).
  function wizardMgApplicable(){
    return declaredChemicalsCache.some(c => c.active !== false && Math.abs(c.potencyMgPerMl || 0) > 0);
  }

  function wizardApplyMgVisibility(rowId, inputId, noteId){
    const applicable = wizardMgApplicable();
    const row = document.getElementById(rowId);
    const input = document.getElementById(inputId);
    const note = noteId ? document.getElementById(noteId) : null;
    if (row) row.style.display = applicable ? '' : 'none';
    if (note) note.style.display = applicable ? 'none' : '';
    if (!applicable && input && !input.value) input.value = 1350; // plausible placeholder, unused by the allocator either way
  }

  async function wizardSkipToDashboard(){
    try { await api('/api/setup-wizard/complete', 'POST', {}); }
    catch (e) { console.error('setup-wizard/complete failed', e); }
    document.getElementById('setupWizardOverlay').style.display = 'none';
  }

  async function wizardSaveTankInfo(){
    const errEl = document.getElementById('wizardStep2Error');
    errEl.textContent = '';
    const gal = parseFloat(document.getElementById('wizardTankGal').value);
    if (!Number.isFinite(gal) || gal <= 0) {
      errEl.textContent = 'Enter a valid tank volume in gallons.';
      return;
    }
    try {
      await fetch('/api/config/volume', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ gallons: gal, volume: gal * 3.78541 })
      });
      const coralLoad = document.getElementById('wizardCoralLoad').value;
      // kalk/cacl2/naoh/mg baseline mL/day left at 0 -- wizard's simplified
      // cold-start; refinable later via the AI Baseline Demand card.
      await api('/api/config/ai-baseline', 'POST', { coralLoad, kalk: 0, cacl2: 0, naoh: 0, mg: 0 });
    } catch (e) {
      errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    wizardGoTo(3);
  }

  function renderWizardChemList(){
    const wrap = document.getElementById('wizardChemList');
    const pill = document.getElementById('wizardChemCountPill');
    if (!wrap) return;
    if (pill) pill.textContent = declaredChemicalsCache.length + ' declared';

    const clearBtn = document.getElementById('wizardClearAllBtn');
    const clearNote = document.getElementById('wizardClearAllNote');
    const hasExisting = declaredChemicalsCache.length > 0;
    if (clearBtn) clearBtn.style.display = hasExisting ? '' : 'none';
    if (clearNote) clearNote.style.display = hasExisting ? '' : 'none';

    const sorted = declaredChemicalsCache.slice().sort((a,b) => a.pumpIndex - b.pumpIndex);
    wrap.innerHTML = sorted.map(c => {
      const presetMatch = chemicalPresetsCache.find(p => p.id === c.presetId);
      const presetName = c.presetId >= 0 && presetMatch ? presetMatch.name : 'Custom';
      const safeName = String(c.name).replace(/'/g, "\\'");
      return `
        <div class="cal-item">
          <div class="cal-top">
            <div>
              <div class="cal-name">${c.name}</div>
              <div class="cal-sub">Pump ${c.pumpIndex + 1} \u2022 ${presetName}</div>
            </div>
          </div>
          <button class="danger" style="margin-top:8px" onclick="removeChemicalConfirm('${c.id}', '${safeName}').then(renderWizardChemList)">Remove</button>
        </div>
      `;
    }).join('');
  }

  async function wizardClearAllChemicals(){
    if (!confirm('Remove all ' + declaredChemicalsCache.length + ' currently-declared chemicals? This cannot be undone.')) return;
    const btn = document.getElementById('wizardClearAllBtn');
    if (btn) btn.disabled = true;
    // Sequential, not parallel -- reuses the exact same validated
    // /api/chemicals/remove endpoint each individual Remove button already
    // calls (same §5.2 sufficiency check), just looped. Pre-baseline-test,
    // nothing has a real correction need yet, so this should never hit
    // that hard block in practice -- but if it ever does on a real device,
    // stop and show the customer which one failed rather than silently
    // leaving a partial clear.
    const ids = declaredChemicalsCache.map(c => c.id);
    for (const id of ids) {
      try {
        const result = await api('/api/chemicals/remove', 'POST', { id });
        if (!result.ok) {
          alert('Stopped: ' + (result.error || 'Failed to remove a chemical.'));
          if (btn) btn.disabled = false;
          await loadDeclaredChemicals();
          return;
        }
      } catch (e) {
        alert('Could not reach the device. Check your connection and try again.');
        if (btn) btn.disabled = false;
        await loadDeclaredChemicals();
        return;
      }
    }
    if (btn) btn.disabled = false;
    await loadDeclaredChemicals();
  }

  function wizardPopulatePresetDropdown(){
    const sel = document.getElementById('wizardNewChemPreset');
    if (!sel) return;
    const custom = '<option value="-1">Something else / I know my potency</option>';
    sel.innerHTML = custom + chemicalPresetsCache.map(p => {
      const tags = [p.movesAlk ? 'Alk' : null, p.movesCa ? 'Ca' : null, p.movesMg ? 'Mg' : null].filter(Boolean).join('+');
      return `<option value="${p.id}">${p.name} (${tags}${p.phSensitive ? ', raises pH' : ''})</option>`;
    }).join('');
  }

  function wizardShowAddChemicalForm(){
    if (freePumpIndexes().length === 0) {
      alert('All 4 physical pumps already have a chemical assigned. Remove one first.');
      return;
    }
    document.getElementById('wizardAddChemicalForm').style.display = 'block';
    document.getElementById('wizardAddChemicalError').textContent = '';
    const sel = document.getElementById('wizardNewChemPump');
    sel.innerHTML = freePumpIndexes().map(i => `<option value="${i}">Pump ${i + 1}</option>`).join('');
    wizardOnNewChemPresetChange();
  }

  function wizardHideAddChemicalForm(){
    document.getElementById('wizardAddChemicalForm').style.display = 'none';
    document.getElementById('wizardNewChemName').value = '';
    document.getElementById('wizardRecipeGrams').value = '';
    document.getElementById('wizardNewChemAlk').value = '';
    document.getElementById('wizardNewChemCa').value = '';
    document.getElementById('wizardNewChemMg').value = '';
    document.getElementById('wizardNewChemPhSensitive').checked = false;
  }

  function wizardOnNewChemPresetChange(){
    const presetId = parseInt(document.getElementById('wizardNewChemPreset').value, 10);
    const customFields = document.getElementById('wizardNewChemCustomFields');
    const recipeFields = document.getElementById('wizardRecipeFields');
    if (customFields) customFields.style.display = presetId < 0 ? 'block' : 'none';

    const recipeInfo = WIZARD_DIY_RECIPES[presetId];
    if (recipeInfo) {
      recipeFields.style.display = 'block';
      document.getElementById('wizardRecipeGrams').value = recipeInfo.standardGrams;
      document.getElementById('wizardRecipeHint').textContent =
        `Standard recipe: ${recipeInfo.standardGrams}g per gallon of RO/DI water. Change this if you mix a different concentration -- potency scales with it automatically.`;
    } else {
      recipeFields.style.display = 'none';
    }
  }

  async function wizardSubmitAddChemical(){
    const errEl = document.getElementById('wizardAddChemicalError');
    errEl.textContent = '';

    const name = document.getElementById('wizardNewChemName').value.trim();
    const presetId = parseInt(document.getElementById('wizardNewChemPreset').value, 10);
    const pumpIndex = parseInt(document.getElementById('wizardNewChemPump').value, 10);

    if (!name) { errEl.textContent = 'Name is required.'; return; }
    if (!Number.isFinite(pumpIndex)) { errEl.textContent = 'No free pump available.'; return; }

    const body = { name, pumpIndex, presetId };
    const recipeInfo = WIZARD_DIY_RECIPES[presetId];

    if (recipeInfo) {
      // DIY recipe scaling: the preset's fixed per-gallon constant scaled
      // by (customer's actual grams / standard recipe's grams), then
      // divided by tank volume -- same formula presetToDeclaration() uses
      // server-side, just parameterized by the customer's real recipe
      // instead of assuming the standard one. Submitted as Custom (-1)
      // since the backend always recomputes potency from scratch for any
      // presetId >= 0, ignoring submitted potency values.
      const preset = chemicalPresetsCache.find(p => p.id === presetId);
      const grams = parseFloat(document.getElementById('wizardRecipeGrams').value);
      if (!Number.isFinite(grams) || grams <= 0) {
        errEl.textContent = 'Enter the grams per gallon you actually mix.';
        return;
      }
      const tankGal = Number(currentStatus.tankGallons) || 0;
      if (!(tankGal > 0)) {
        errEl.textContent = 'Tank volume isn\u2019t set yet -- go back to Step 2 first.';
        return;
      }
      const scale = grams / recipeInfo.standardGrams;
      body.presetId = -1;
      body.potencyAlkPerMl = (preset.potencyAlkPerGallon || 0) * scale / tankGal;
      body.potencyCaPerMl  = (preset.potencyCaPerGallon  || 0) * scale / tankGal;
      body.potencyMgPerMl  = (preset.potencyMgPerGallon  || 0) * scale / tankGal;
      // Added 2026-08-04: same gap as Manage Chemicals' equivalent
      // function -- was silently missing, even after real pH values
      // existed for NaOH/soda-ash in ChemicalPresets.cpp.
      body.potencyPhPerMl  = (preset.potencyPhPerGallon  || 0) * scale / tankGal;
      body.phSensitive = preset.phSensitive;
    } else if (presetId < 0) {
      body.potencyAlkPerMl = parseFloat(document.getElementById('wizardNewChemAlk').value) || 0;
      body.potencyCaPerMl = parseFloat(document.getElementById('wizardNewChemCa').value) || 0;
      body.potencyMgPerMl = parseFloat(document.getElementById('wizardNewChemMg').value) || 0;
      body.phSensitive = document.getElementById('wizardNewChemPhSensitive').checked;
    }
    // else: commercial preset or saturated Kalkwasser -- presetId >= 0,
    // backend computes fixed potency from tank volume alone, no override.

    try {
      let result = await api('/api/chemicals', 'POST', body);
      // Added 2026-08-04: same confirm-and-resubmit flow as the main
      // dashboard's submitAddChemical -- see that function's comment.
      if (!result.ok && (result.error || '').indexOf('generic placeholder') >= 0) {
        const reallySure = confirm(
          (result.error || '') +
          '\n\nClick OK only if you have actually confirmed this tank\'s real mixed strength ' +
          'matches this number -- not because it looked like a reasonable default.'
        );
        if (!reallySure) {
          errEl.textContent = 'Not added -- enter this tank\'s actual measured strength.';
          return;
        }
        body.confirmGenericValue = true;
        result = await api('/api/chemicals', 'POST', body);
      }
      if (!result.ok) {
        errEl.textContent = result.error || 'Failed to add chemical.';
        return;
      }
    } catch (e) {
      errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }

    wizardHideAddChemicalForm();
    await loadDeclaredChemicals();
    renderWizardChemList();
  }

  // Pump calibration: one row per declared chemical's pump, wizard-scoped
  // element IDs (the main dashboard's Flow Calibration card sits behind
  // this full-screen overlay and its IDs aren't usable from here -- same
  // lesson as the add-chemical form).
  function renderWizardCalibrationList(){
    const wrap = document.getElementById('wizardCalibrationList');
    if (!wrap) return;
    const sorted = declaredChemicalsCache.slice().sort((a,b) => a.pumpIndex - b.pumpIndex);
    wrap.innerHTML = sorted.map(c => `
      <div class="cal-item">
        <div class="cal-top">
          <div>
            <div class="cal-name">${c.name}</div>
            <div class="cal-sub">Pump ${c.pumpIndex + 1}</div>
          </div>
          <span class="flow-badge" id="wizardFlowBadge_${c.pumpIndex}">not calibrated</span>
        </div>
        <div class="three" style="margin-top:8px">
          <button class="sec" onclick="wizardRunCalibration(${c.pumpIndex}, 30)">Run 30 sec</button>
          <button class="sec" onclick="wizardRunCalibration(${c.pumpIndex}, 60)">Run 60 sec</button>
          <input type="number" step="0.1" id="wizardMeasuredMl_${c.pumpIndex}" placeholder="mL collected">
        </div>
        <button class="sec" style="margin-top:8px" onclick="wizardSaveCalibration(${c.pumpIndex})">Save Flow Rate</button>
        <div class="help" id="wizardCalError_${c.pumpIndex}" style="color:var(--danger);margin-top:6px"></div>
      </div>
    `).join('');
    // Track the seconds used for each pump's last run, so Save can compute
    // mL/min without asking the customer to re-enter it.
    window.wizardLastRunSeconds = window.wizardLastRunSeconds || {};
  }

  async function wizardRunCalibration(pumpIndex, seconds){
    window.wizardLastRunSeconds = window.wizardLastRunSeconds || {};
    window.wizardLastRunSeconds[pumpIndex] = seconds;
    const errEl = document.getElementById('wizardCalError_' + pumpIndex);
    if (errEl) errEl.textContent = '';
    try {
      const res = await api('/api/calibration-run', 'POST', { pumpIndex, seconds });
      if (res && res.ok === false) {
        if (errEl) errEl.textContent = res.error || 'Calibration run failed.';
        return;
      }
    } catch (e) {
      if (errEl) errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    if (errEl) errEl.textContent = `Ran for ${seconds}s -- measure the mL collected, enter it below, then Save.`;
  }

  async function wizardSaveCalibration(pumpIndex){
    const errEl = document.getElementById('wizardCalError_' + pumpIndex);
    errEl.textContent = '';
    const measuredMl = parseFloat(document.getElementById('wizardMeasuredMl_' + pumpIndex).value);
    const seconds = (window.wizardLastRunSeconds && window.wizardLastRunSeconds[pumpIndex]) || 60;
    if (!Number.isFinite(measuredMl) || measuredMl <= 0) {
      errEl.textContent = 'Enter the actual mL collected during the run.';
      return;
    }
    const flowMlPerMin = measuredMl * 60.0 / seconds;
    try {
      const res = await api('/api/calibration', 'POST', { pumpIndex, flowMlPerMin });
      if (res && res.ok === false) {
        errEl.textContent = res.error || 'Failed to save flow rate.';
        return;
      }
    } catch (e) {
      errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    const badge = document.getElementById('wizardFlowBadge_' + pumpIndex);
    if (badge) badge.textContent = flowMlPerMin.toFixed(2) + ' mL/min';
    errEl.textContent = '';
  }

  function wizardUseRecommendedTargets(){
    document.getElementById('wizardTargetAlk').value = 8.5;
    document.getElementById('wizardTargetCa').value = 450;
    document.getElementById('wizardTargetMg').value = 1440;
    document.getElementById('wizardTargetPh').value = 8.30;
  }

  async function wizardSaveTargets(){
    const errEl = document.getElementById('wizardStep5Error');
    errEl.textContent = '';
    const alk = parseFloat(document.getElementById('wizardTargetAlk').value);
    const ca = parseFloat(document.getElementById('wizardTargetCa').value);
    const mg = parseFloat(document.getElementById('wizardTargetMg').value);
    const ph = parseFloat(document.getElementById('wizardTargetPh').value);
    if (![alk, ca, mg].every(Number.isFinite)) {
      errEl.textContent = 'Enter all three targets (Alk, Ca, Mg) -- or click "Use Recommended Defaults."';
      return;
    }
    if (!Number.isFinite(ph) || ph < 7.80 || ph > 8.80) {
      errEl.textContent = 'Enter a pH target between 7.80 and 8.80.';
      return;
    }
    try {
      const result = await api('/api/config/chemistry-targets', 'POST', { alk, ca, mg });
      if (result && result.ok === false) {
        errEl.textContent = result.error || 'Failed to save targets.';
        return;
      }
      // Partial update -- only naohMaxPh is sent, so the day/night split
      // percentages (configured elsewhere, or defaulted by legacy
      // migration) are left completely untouched. Confirmed safe: the
      // backend handler falls back to the CURRENT value for any field not
      // present in the request body, not a hardcoded default.
      const phResult = await api('/api/config/mode7-split', 'POST', { naohMaxPh: ph });
      if (phResult && phResult.ok === false) {
        errEl.textContent = phResult.error || 'Failed to save pH target.';
        return;
      }
    } catch (e) {
      errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    }
    wizardGoTo(6);
  }

  async function wizardSaveBaselineTest(){
    const errEl = document.getElementById('wizardStep6Error');
    errEl.textContent = '';

    if (declaredChemicalsCache.length === 0) {
      errEl.textContent = 'Add at least one chemical (Step 3) before entering a baseline test.';
      return;
    }

    const alk = parseFloat(document.getElementById('wizardAlk').value);
    const ca = parseFloat(document.getElementById('wizardCa').value);
    const mg = parseFloat(document.getElementById('wizardMg').value);
    const ph = parseFloat(document.getElementById('wizardPh').value);

    if (![alk, ca, mg, ph].every(Number.isFinite)) {
      errEl.textContent = 'Enter all four values (Alk, Ca, Mg, pH).';
      return;
    }

    // Same double-submit guard as the main dashboard's Manual Water Test
    // card -- see saveTest()'s comment for why this matters specifically
    // for a manual test (always ingested as genuinely new data).
    const btn = document.getElementById('wizardSaveTestBtn');
    if (btn) { if (btn.disabled) return; btn.disabled = true; }
    try {
      const result = await api('/api/manual-test', 'POST', { alk, ca, mg, ph });
      if (result && result.ok === false) {
        errEl.textContent = result.error || 'Failed to submit test.';
        return;
      }
    } catch (e) {
      errEl.textContent = 'Could not reach the device. Check your connection and try again.';
      return;
    } finally {
      if (btn) btn.disabled = false;
    }
    wizardGoTo(7);
  }

  async function wizardFinish(){
    try { await api('/api/setup-wizard/complete', 'POST', {}); }
    catch (e) { console.error('setup-wizard/complete failed', e); }
    document.getElementById('setupWizardOverlay').style.display = 'none';
    await loadAll();
  }

  function checkSetupWizardVisibility(s){
    const overlay = document.getElementById('setupWizardOverlay');
    if (!overlay) return;
    if (s && s.shouldShowSetupWizard) {
      if (overlay.style.display === 'none') wizardGoTo(1);
      overlay.style.display = '';
    } else {
      overlay.style.display = 'none';
    }
  }

  function setPushStatus(msg){
    const el = document.getElementById('pushNotificationStatus');
    if (el) el.textContent = 'Push setup status: ' + msg;
  }

  function firebaseConfigReady(){
    return FIREBASE_WEB_CONFIG.messagingSenderId &&
      !FIREBASE_WEB_CONFIG.messagingSenderId.includes('PASTE_') &&
      FIREBASE_WEB_CONFIG.appId &&
      !FIREBASE_WEB_CONFIG.appId.includes('PASTE_') &&
      FIREBASE_WEB_PUSH_VAPID_KEY &&
      !FIREBASE_WEB_PUSH_VAPID_KEY.includes('PASTE_');
  }

  function initFirebaseAppForDatabase(){
    if (typeof firebase === 'undefined') return false;
    try {
      if (!firebase.apps || !firebase.apps.length) firebase.initializeApp(FIREBASE_WEB_CONFIG);
      return !!firebase.database;
    } catch (err) {
      console.warn('Firebase database init failed', err);
      return false;
    }
  }

  function initFirebasePush(){
    if (firebasePushInitialized) return true;

    if (!window.isSecureContext || location.protocol !== 'https:') {
      setPushStatus('open the Firebase-hosted HTTPS dashboard on iPhone Safari.');
      return false;
    }

    if (typeof firebase === 'undefined') {
      setPushStatus('Firebase SDK did not load.');
      return false;
    }

    if (!firebaseConfigReady()) {
      setPushStatus('Firebase Sender ID, App ID, or VAPID key is still a placeholder.');
      return false;
    }

    if (!initFirebaseAppForDatabase()) {
      setPushStatus('Firebase app init failed.');
      return false;
    }

    firebasePushInitialized = true;
    return true;
  }

  async function enableFirebasePushNotifications(){
    try {
      if (!('Notification' in window)) {
        setPushStatus('this browser does not support notifications.');
        return;
      }
      if (!('serviceWorker' in navigator)) {
        setPushStatus('service worker not supported by this browser.');
        return;
      }
      if (!initFirebasePush()) return;

      const registration = await navigator.serviceWorker.register('/firebase-messaging-sw.js');
      const permission = await Notification.requestPermission();

      if (permission !== 'granted') {
        setPushStatus('permission was denied.');
        return;
      }

      const messaging = firebase.messaging();
      const token = await messaging.getToken({
        vapidKey: FIREBASE_WEB_PUSH_VAPID_KEY,
        serviceWorkerRegistration: registration
      });

      if (!token) {
        setPushStatus('no token returned by Firebase Messaging.');
        return;
      }

      const deviceId = currentStatus.deviceId || 'reefDoser3';
      await firebase.database()
        .ref('/devices/' + deviceId + '/fcmTokens')
        .push({
          token: token,
          platform: navigator.platform || 'web',
          userAgent: navigator.userAgent || '',
          enabled: true,
          createdAt: Date.now(),
          lastSeen: Date.now()
        });

      setPushStatus('enabled for ' + deviceId + '.');
      alert('Push notifications enabled for ' + deviceId + '.');
    } catch (err) {
      console.error(err);
      setPushStatus('failed: ' + (err && err.message ? err.message : err));
      alert('Push setup failed: ' + (err && err.message ? err.message : err));
    }
  }

  async function saveNotificationLevel(){
    const level = normalizeNotificationLevel(document.getElementById('notificationLevelSelect').value);
    const res = await api('/api/config/notifications', 'POST', { notificationLevel: level, alertMode: level });
    if(res && res.ok === false){
      alert('Notification level save failed: ' + (res.error || res.raw || 'unknown error'));
      return;
    }
    currentStatus.notificationLevel = level;
    clearNotificationDirty();
    const pill = document.getElementById('notificationLevelPill');
    if (pill) pill.textContent = notificationLevelLabel(level);
    await loadAll();
    alert('Notification level saved: ' + notificationLevelLabel(level));
  }

  async function saveCalibration(pumpIndex, pumpKey){
    const measuredInput = document.getElementById(`flow_${pumpIndex}`);
    const secInput = document.getElementById(`cal_sec_${pumpIndex}`);
    const measuredMl = parseFloat(measuredInput.value);
    const runSeconds = parseFloat(secInput.value || calibrationLastRunSec[pumpIndex] || 60);

    if(!Number.isFinite(measuredMl) || measuredMl <= 0){
      alert('Enter the actual mL collected during the calibration run.');
      return;
    }
    if(!Number.isFinite(runSeconds) || runSeconds <= 0){
      alert('Enter the number of seconds the pump ran.');
      return;
    }

    const flowMlPerMin = measuredMl * 60.0 / runSeconds;
    const res = await api('/api/calibration', 'POST', { pumpIndex, pump: pumpKey, flowMlPerMin });
    if(res && res.ok === false){
      alert('Calibration save failed: ' + (res.error || res.raw || 'unknown error'));
      return;
    }

    clearCalibrationDirty(pumpIndex);
    measuredInput.value = '';
    const badge = document.getElementById(`flow_badge_${pumpIndex}`);
    if (badge) badge.textContent = flowMlPerMin.toFixed(2) + ' mL/min';
    await loadAll();
    alert(`${pumpKey.toUpperCase()} flow saved: ${flowMlPerMin.toFixed(2)} mL/min`);
  }

  async function runCalibrationPump(pumpIndex, pumpKey, fallbackSeconds){
    const secInput = document.getElementById(`cal_sec_${pumpIndex}`);
    const seconds = Number(fallbackSeconds || (secInput && secInput.value) || 60);

    if(secInput) secInput.value = seconds;
    calibrationLastRunSec[pumpIndex] = seconds;
    markCalibrationDirty(pumpIndex);

    if(!confirm(`Run ${pumpKey.toUpperCase()} pump ${pumpIndex + 1} for exactly ${seconds} seconds?`)) return;

    const res = await api('/api/calibration-run', 'POST', { pumpIndex, pump: pumpKey, seconds });
    if(res && res.ok === false){
      alert('Calibration run failed: ' + (res.error || res.raw || 'unknown error'));
      return;
    }
    alert(`Calibration run started for pump ${pumpIndex + 1} for ${seconds} seconds. Measure the actual mL collected, enter it, then press Save.`);
  }

  async function saveApex(){
    await api('/api/config/apex','POST',{
      enabled: document.getElementById('apEn').value === '1',
      ip: document.getElementById('apIp').value.trim()
    });
    await loadAll();
    alert('Apex config saved.');
  }

  async function saveTest(){
    // Fixed 2026-07-31: a real accidental double-click submitted the same
    // manual test twice in a row on reefDoser1, and since manual tests are
    // always treated as genuinely new data (unlike a repeated Apex poll of
    // an unchanged reading -- see runAiRecalculation()'s isNewMeasurement
    // comment), the Kalman filter ingested it as two independent
    // confirming measurements instead of one, inflating proven maturity
    // beyond what the single real test should have produced. Disabling the
    // button for the duration of the request is a normal, simple guard
    // against exactly that -- not a data-correctness fix inside the engine
    // (which correctly can't distinguish "the same reading, submitted
    // twice by mistake" from "the same reading, genuinely confirmed by a
    // second real test" -- that distinction belongs at the UI layer).
    const btn = document.getElementById('saveTestBtn');
    if (btn) { if (btn.disabled) return; btn.disabled = true; }
    try {
      await api('/api/manual-test','POST',{
        alk: parseFloat(document.getElementById('alk').value),
        ca: parseFloat(document.getElementById('ca').value),
        mg: parseFloat(document.getElementById('mg').value),
        ph: parseFloat(document.getElementById('ph').value)
      });
      await loadAll();
      alert('AI recalculated.');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  async function liveDose(){
    const pumpSelectEl = document.getElementById('pSel');
    const pumpIndex = parseInt(pumpSelectEl.value, 10);
    const pumpLabel = pumpSelectEl.options[pumpSelectEl.selectedIndex]
      ? pumpSelectEl.options[pumpSelectEl.selectedIndex].text
      : ('pump index ' + pumpIndex);
    const ml = parseFloat(document.getElementById('v_ml').value);
    if(!Number.isFinite(ml) || ml <= 0){
      alert('Enter a valid dose amount in mL.');
      return;
    }
    // Fixed 2026-08-04: previously alerted "Dose command sent." unconditionally,
    // regardless of whether the backend actually accepted the request -- found
    // while chasing "Quick Dose only works for Pump 1," where the real cause
    // (pumpCountForCurrentDosingMode() rejecting pumps 2-4, now fixed
    // separately in WebRoutes.cpp) was invisible here because this alert never
    // checked the response at all. A rejected request (inter-pump delay,
    // emergency stop, safety cap, invalid pump) looked IDENTICAL to a real
    // success from this dialog alone. Also now names the actual pump/amount
    // sent, not a generic message -- a separate real bug (renderPumpSelect()
    // silently reverting the dropdown to Pump 1 on a background refresh, see
    // that function's fix above) could otherwise dose the wrong pump with no
    // visible sign of it.
    let res;
    try {
      res = await api('/api/live-dose','POST',{ pumpIndex, ml });
    } catch (e) {
      alert('Could not reach the device. Check your connection and try again.');
      return;
    }
    if (!res || res.ok === false) {
      alert('Dose failed (' + pumpLabel + ', ' + ml + ' mL): ' + ((res && (res.error || res.raw)) || 'unknown error'));
      return;
    }
    alert('Dose command sent: ' + pumpLabel + ', ' + ml + ' mL.');
  }
async function saveLightConfig() {
  const payload = {
    source: parseInt(gv('lightSrc')),
    start: parseInt(gv('lStart')),
    end: parseInt(gv('lEnd')),
    outlet: gv('lOutlet')
  };

  if (![0, 1].includes(payload.source)) {
    alert('Select a valid light detection source.');
    return;
  }
  if (!Number.isFinite(payload.start) || payload.start < 0 || payload.start > 23) {
    alert('Select a valid light start hour.');
    return;
  }
  if (!Number.isFinite(payload.end) || payload.end < 0 || payload.end > 23) {
    alert('Select a valid light end hour.');
    return;
  }

  const res = await api('/api/config/lights', 'POST', payload);
  if (res && res.ok === false) {
    alert('Light schedule save failed: ' + (res.error || res.raw || 'unknown error'));
    return;
  }

  currentStatus.lightConfig = payload;
  clearLightDirty();
  await loadAll();
  alert("Metabolic Map Updated");
}



// Removed 2026-08-02: saveCalciumDemandLearningFromToggle, saveAlkDemandLearningFromToggle,
// estimateAiBaseline, saveAiBaseline -- all four provably orphaned (verified
// zero remaining callers) after their corresponding dashboard cards
// (7-Day Alk/Calcium Demand Learning, AI Baseline Demand) were removed.


function recipeTankGallons(){
  const inputGal = Number(gv('tankGal'));
  if (Number.isFinite(inputGal) && inputGal > 0) return inputGal;

  const s = currentStatus || {};
  const candidates = [
    Number(s.tankGallons),
    Number(s.tankGal),
    Number(s.gallons),
    Number(s.tankVolumeGallons),
    Number(s.tankVolumeGal)
  ];

  for (const gal of candidates) {
    if (Number.isFinite(gal) && gal > 0) return gal;
  }

  const liters = Number(s.tankLiters ?? s.tankVolumeLiters ?? s.volumeLiters ?? s.volume);
  if (Number.isFinite(liters) && liters > 0) return liters / 3.78541;

  return 0;
}

function getRecipeUnitMode(){
  const el = document.getElementById('recipeUnitMode');
  return el && el.value === 'liter' ? 'liter' : 'gallon';
}

function setRecipeUnitLabels(){
  const label = getRecipeUnitMode() === 'liter' ? 'grams per liter' : 'grams per gallon';
  document.querySelectorAll('.recipeUnitLabel').forEach(el => el.textContent = label);
}

function formatRecipeValue(value){
  const n = Number(value);
  if (!Number.isFinite(n)) return '';
  const digits = n >= 100 ? 0 : (n >= 10 ? 1 : 2);
  return n.toFixed(digits).replace(/\.0$/, '');
}

function setRecipeInputFromGpg(id, gpg){
  const el = document.getElementById(id);
  if (!el) return;
  const shown = getRecipeUnitMode() === 'liter' ? Number(gpg) / 3.78541 : Number(gpg);
  el.value = formatRecipeValue(shown);
}

function recipeInputToGpg(id, defaultGpg){
  const el = document.getElementById(id);
  const shown = el ? Number(el.value) : NaN;
  const value = Number.isFinite(shown) && shown >= 0 ? shown : Number(defaultGpg);
  return getRecipeUnitMode() === 'liter' ? value * 3.78541 : value;
}

function recipeAmountTextFromGpg(gpg, tspLabel){
  const unit = getRecipeUnitMode();
  const shown = unit === 'liter' ? Number(gpg) / 3.78541 : Number(gpg);
  const unitText = unit === 'liter' ? 'liter' : 'gallon';
  const amount = formatRecipeValue(shown);
  if (tspLabel && unit === 'gallon') {
    return `${tspLabel}, about ${amount} g per ${unitText}`;
  }
  return `${amount} g per ${unitText}`;
}

function onRecipeUnitChange(){
  // Convert the visible AI Doser Standard values when the user switches unit display.
  const unit = getRecipeUnitMode();
  setRecipeUnitLabels();

  const sourceVal = (id, fallback) => {
    const el = document.getElementById(id);
    return el ? el.value : fallback;
  };

  // Keep custom entries by converting their current visible value.
  // If source is AI standard, use known defaults.
  const alkSrc = sourceVal('recipeAlkSource', 'aid_standard');
  const naohSrc = sourceVal('recipeNaohSource', 'aid_standard');
  const caSrc = sourceVal('recipeCacl2Source', 'aid_standard');
  const mgSrc = sourceVal('recipeMgSource', 'aid_standard');

  setRecipeInputFromGpg('recipeKalkGpg', 12);
  if (alkSrc === 'aid_standard') setRecipeInputFromGpg('recipeAlkGpg', 100);
  if (naohSrc === 'aid_standard') setRecipeInputFromGpg('recipeNaohGpg', 144);
  if (caSrc === 'aid_standard') setRecipeInputFromGpg('recipeCacl2Gpg', 250);
  if (mgSrc === 'aid_standard') setRecipeInputFromGpg('recipeMgGpg', 500);

  updateRecipeNotes();
  updateRecipePreview();
}

function toggleAfrCustom(){
  const box = document.getElementById('afrCustomBox');
  const type = gv('recipeAfrType') || 'tm_afr_powder';
  if (box) box.style.display = type === 'custom' ? 'block' : 'none';
}

function applyRecipeSourceDefaults(){
  const unit = getRecipeUnitMode();
  setRecipeUnitLabels();

  if ((gv('recipeAlkSource') || 'aid_standard') === 'aid_standard') setRecipeInputFromGpg('recipeAlkGpg', 100);
  if ((gv('recipeNaohSource') || 'aid_standard') === 'aid_standard') setRecipeInputFromGpg('recipeNaohGpg', 144);
  if ((gv('recipeCacl2Source') || 'aid_standard') === 'aid_standard') setRecipeInputFromGpg('recipeCacl2Gpg', 250);
  if ((gv('recipeMgSource') || 'aid_standard') === 'aid_standard') setRecipeInputFromGpg('recipeMgGpg', 500);

  updateRecipeNotes();
  updateRecipePreview();
}

function calcRecipeStrengths(){
  const gal = recipeTankGallons();
  if (!Number.isFinite(gal) || gal <= 0) return null;

  const kalkGpg = recipeInputToGpg('recipeKalkGpg', 12);
  const alkGpg = recipeInputToGpg('recipeAlkGpg', 100);
  const naohGpg = recipeInputToGpg('recipeNaohGpg', 144);
  const mgGpg = recipeInputToGpg('recipeMgGpg', 500);
  const cacl2Gpg = recipeInputToGpg('recipeCacl2Gpg', 250);

  const alkType = gv('recipeAlkType') || 'soda_ash';
  const mgType = gv('recipeMgType') || 'mag_chloride';
  const cacl2Type = gv('recipeCacl2Type') || 'cacl2_dihydrate';
  const afrType = gv('recipeAfrType') || 'tm_afr_powder';

  const alkFactor = alkType === 'baking_soda' ? 0.00252 : 0.00400;
  const mgFactor = mgType === 'epsom' ? 0.373 : 0.452;
  const cacl2Factor = cacl2Type === 'cacl2_anhydrous' ? 1.365 : 1.034;

  // Tropic Marin AFR powder standard solution: 160 g per final liter.
  // Internal dashboard strength is dKH change per 1 mL added to this tank.
  const afrPresetStrength = 6.0 / (gal * 3.78541);
  const afrStrength = afrType === 'tm_afr_powder' ? afrPresetStrength : (Number(gv('strAfr')) || 0.0000001);

  return {
    gal,
    recipe: {
      kalkGpg,
      afrGpg: 160,
      afrType,
      alkGpg,
      naohGpg,
      mgGpg,
      cacl2Gpg,
      alkType,
      mgType,
      cacl2Type
    },
    strengths: {
      kalk: (0.00573 * kalkGpg) / gal,
      afr: afrStrength,
      alk: (alkFactor * alkGpg) / gal,
      naoh: (0.00530 * naohGpg) / gal,
      mg: (mgFactor * mgGpg) / gal,
      cacl2: (cacl2Factor * cacl2Gpg) / gal
    }
  };
}

function updateRecipeNotes(){
  const setHtml = (id, html) => {
    const el = document.getElementById(id);
    if (el) el.innerHTML = html;
  };

  const unit = getRecipeUnitMode();
  const afrType = gv('recipeAfrType') || 'tm_afr_powder';
  const alkType = gv('recipeAlkType') || 'soda_ash';
  const cacl2Type = gv('recipeCacl2Type') || 'cacl2_dihydrate';
  const mgType = gv('recipeMgType') || 'mag_chloride';

  const kalkGpg = recipeInputToGpg('recipeKalkGpg', 12);
  const alkGpg = recipeInputToGpg('recipeAlkGpg', 100);
  const naohGpg = recipeInputToGpg('recipeNaohGpg', 144);
  const cacl2Gpg = recipeInputToGpg('recipeCacl2Gpg', 250);
  const mgGpg = recipeInputToGpg('recipeMgGpg', 500);

  setHtml('noteKalk', `<b>AI Doser Standard:</b> Saturated kalk = ${recipeAmountTextFromGpg(kalkGpg, '2 tsp')} of RO/DI water.`);

  if (afrType === 'custom') {
    setHtml('noteAfr', '<b>Custom AFR:</b> Use the bottle label or measured tank result to enter dKH per mL.');
  } else if (unit === 'liter') {
    setHtml('noteAfr', '<b>Tropic Marin Standard:</b> AFR powder = 160 g per final 1 liter dosing solution.');
  } else {
    setHtml('noteAfr', '<b>Tropic Marin Standard:</b> AFR powder = about 606 g per final 1 gallon dosing solution.');
  }

  if (alkType === 'baking_soda') {
    setHtml('noteAlk', `<b>AI Doser Standard:</b> Baking Soda / Sodium Bicarbonate = ${recipeAmountTextFromGpg(alkGpg)} of RO/DI water.`);
  } else {
    setHtml('noteAlk', `<b>AI Doser Standard:</b> Soda Ash / Sodium Carbonate = ${recipeAmountTextFromGpg(alkGpg)} of RO/DI water.`);
  }

  setHtml('noteNaoh', `<b>AI Doser Standard:</b> ${recipeAmountTextFromGpg(naohGpg)} NaOH in RO/DI water. Do not change unless you intentionally mix a different concentration.`);

  if (cacl2Type === 'cacl2_anhydrous') {
    setHtml('noteCacl2', `<b>AI Doser Standard:</b> Anhydrous Calcium Chloride = ${recipeAmountTextFromGpg(cacl2Gpg)} of RO/DI water.`);
  } else {
    setHtml('noteCacl2', `<b>AI Doser Standard:</b> Calcium Chloride Dihydrate = ${recipeAmountTextFromGpg(cacl2Gpg)} of RO/DI water.`);
  }

  if (mgType === 'epsom') {
    setHtml('noteMg', `<b>AI Doser Standard:</b> Epsom Salt / Magnesium Sulfate = ${recipeAmountTextFromGpg(mgGpg)} of RO/DI water.`);
  } else {
    setHtml('noteMg', `<b>AI Doser Standard:</b> Magnesium Chloride Hexahydrate = ${recipeAmountTextFromGpg(mgGpg)} of RO/DI water.`);
  }
}

function updateRecipePreview(){
  setRecipeUnitLabels();
  toggleAfrCustom();
  updateRecipeNotes();

  const calc = calcRecipeStrengths();
  const el = document.getElementById('recipePreview');
  const tankEl = document.getElementById('recipeTankSummary');

  const setText = (id, text) => {
    const node = document.getElementById(id);
    if (node) node.textContent = text;
  };

  if (!calc) {
    if (el) el.innerHTML = '<b>Enter tank volume first.</b> Use the Tank Volume field at the top of the dashboard.';
    if (tankEl) tankEl.textContent = 'Tank volume needed';
    ['calcKalk','calcAfr','calcAlk','calcNaoh','calcCacl2','calcMg'].forEach(id => setText(id, 'enter tank'));
    return;
  }

  const s = calc.strengths;
  if (tankEl) tankEl.textContent = `Tank ${calc.gal.toFixed(1)} gal`;

  setText('calcKalk', s.kalk.toFixed(7));
  setText('calcAfr', s.afr.toFixed(7));
  setText('calcAlk', s.alk.toFixed(7));
  setText('calcNaoh', s.naoh.toFixed(7));
  setText('calcCacl2', s.cacl2.toFixed(5));
  setText('calcMg', s.mg.toFixed(5));

  if (el) {
    el.innerHTML =
      `<b>Using tank volume:</b> ${calc.gal.toFixed(1)} gallons<br>` +
      `<b>Kalk:</b> ${s.kalk.toFixed(7)} dKH/mL &nbsp; ` +
      `<b>AFR:</b> ${s.afr.toFixed(7)} dKH/mL &nbsp; ` +
      `<b>Alk:</b> ${s.alk.toFixed(7)} dKH/mL &nbsp; ` +
      `<b>NaOH:</b> ${s.naoh.toFixed(7)} dKH/mL<br>` +
      `<b>CaCl2:</b> ${s.cacl2.toFixed(5)} ppm/mL &nbsp; ` +
      `<b>Mg:</b> ${s.mg.toFixed(5)} ppm/mL`;
  }

  const setVal = (id, val, digits) => {
    const input = document.getElementById(id);
    if (input && document.activeElement !== input) input.value = Number(val).toFixed(digits);
  };

  setVal('strKalk', s.kalk, 7);
  setVal('strAlk', s.alk, 7);
  setVal('strNaoh', s.naoh, 7);
  setVal('strMg', s.mg, 5);
  setVal('strCacl2', s.cacl2, 5);
}

// Removed 2026-08-02: saveChemicalStrengths -- provably orphaned (zero
// remaining callers) after the Chemical Recipes -> Strengths card was
// removed. calcRecipeStrengths() itself left in place; still used by the
// (now display-only, unsaved) recipe preview elsewhere in this file.



function pumpSafetyName(index){
  return pumpChemicalName(index, currentDosingMode || 1);
}

function renderPumpSafetyInputs(s){
  const safeties = (s && s.pumpSafeties) || {};
  const defaults = [
    {thresholdMl:100, maxDoseMl:1500, maxDayMl:35000},
    {thresholdMl:10,  maxDoseMl:250,  maxDayMl:2000},
    {thresholdMl:5,   maxDoseMl:100,  maxDayMl:1200},
    {thresholdMl:5,   maxDoseMl:100,  maxDayMl:2500}
  ];

  for (let i = 0; i < 4; i++) {
    const key = 'p' + (i + 1);
    const row = safeties[key] || defaults[i];
    const nameEl = document.getElementById('safetyPumpName' + i);
    const usedEl = document.getElementById('safetyUsed' + i);
    const thrEl = document.getElementById('safeThr' + i);
    const maxEl = document.getElementById('safeMax' + i);
    const dayEl = document.getElementById('safeDay' + i);

    if (nameEl) nameEl.textContent = 'P' + (i + 1) + ' ' + pumpSafetyName(i);
    const used = Number(row.usedTodayMl || 0);
    const remaining = Number(row.remainingTodayMl || 0);
    if (usedEl) usedEl.textContent = 'Used today: ' + used.toFixed(1) + ' mL • left: ' + remaining.toFixed(1) + ' mL';

    if (thrEl) thrEl.value = Number(row.thresholdMl ?? defaults[i].thresholdMl).toFixed(1).replace(/\.0$/, '');
    if (maxEl) maxEl.value = Number(row.maxDoseMl ?? defaults[i].maxDoseMl).toFixed(0);
    if (dayEl) dayEl.value = Number(row.maxDayMl ?? defaults[i].maxDayMl).toFixed(0);
  }
}

function readPumpSafetyRow(i){
  const thresholdMl = parseFloat(gv('safeThr' + i));
  const maxDoseMl = parseFloat(gv('safeMax' + i));
  const maxDayMl = parseFloat(gv('safeDay' + i));

  if (!Number.isFinite(thresholdMl) || thresholdMl <= 0) throw new Error('Enter a valid threshold for P' + (i + 1));
  if (!Number.isFinite(maxDoseMl) || maxDoseMl <= 0) throw new Error('Enter a valid max single dose for P' + (i + 1));
  if (!Number.isFinite(maxDayMl) || maxDayMl <= 0) throw new Error('Enter a valid max daily dose for P' + (i + 1));

  return { thresholdMl, maxDoseMl, maxDayMl };
}

async function saveDosingSafeties() {
  let pumpSafeties;
  try {
    pumpSafeties = {
      p1: readPumpSafetyRow(0),
      p2: readPumpSafetyRow(1),
      p3: readPumpSafetyRow(2),
      p4: readPumpSafetyRow(3)
    };
  } catch (err) {
    alert(err.message || err);
    return;
  }

  const payload = { pumpSafeties };
  const res = await api('/api/config/safeties', 'POST', payload);
  if (res && res.ok === false) {
    alert('Safety save failed: ' + (res.error || res.raw || 'unknown error'));
    return;
  }

  currentStatus.pumpSafeties = pumpSafeties;
  currentStatus.dosingThreshold = pumpSafeties.p1.thresholdMl;
  currentStatus.maxHourlyLimit = pumpSafeties.p1.maxDoseMl;
  clearSafetyDirty();

  await loadAll();
  alert("Pump Safety Rails Updated");
}


function renderAiChemistrySafeties(s){
  const cfg = (s && s.aiChemistrySafeties) || {};
  const setVal = (id, value, digits=0) => {
    const el = document.getElementById(id);
    if (el) el.value = Number(value).toFixed(digits).replace(/\.0$/, '');
  };

  setVal('aiSafeMaxKalk', cfg.maxKalkDayMl ?? 35000, 0);
  setVal('aiSafeMaxNaoh', cfg.maxNaohDayMl ?? 1200, 0);
  setVal('aiSafeMaxAlk', cfg.maxAlkDayMl ?? 2500, 0);
  setVal('aiSafeMaxAlkRise', cfg.maxAlkRiseDkhDay ?? 2.0, 2);
  setVal('aiSafeMaxMgCorrection', cfg.maxMgCorrectionDayMl ?? 250, 0);
  setVal('aiSafeMaxMg', cfg.maxMgDayMl ?? 250, 0);
  setVal('aiSafeMgDeadband', cfg.mgDeadbandPpm ?? 25, 0);
}

function readPositiveNumber(id, label, allowZero=false){
  const n = Number(gv(id));
  if (!Number.isFinite(n) || (allowZero ? n < 0 : n <= 0)) throw new Error('Enter a valid ' + label + '.');
  return n;
}

// Removed 2026-08-02: saveAiChemistrySafeties -- provably orphaned (zero
// remaining callers) after the AI Chemistry Safeties card was removed.

function renderMode7DayNightSplit(s){
  const cfg = (s && s.mode7DayNightSplit) || {};
  const setVal = (id, value) => {
    const el = document.getElementById(id);
    if (el) el.value = String(value);
  };

  setVal('m7SplitEnabled', cfg.enabled === false ? 0 : 1);
  setVal('m7DayNaohPct', Number(cfg.dayNaohPct ?? 0).toFixed(0));
  setVal('m7DayAlkPct', Number(cfg.dayAlkPct ?? 100).toFixed(0));
  setVal('m7NightNaohPct', Number(cfg.nightNaohPct ?? 100).toFixed(0));
  setVal('m7NightAlkPct', Number(cfg.nightAlkPct ?? 0).toFixed(0));
  setVal('m7NaohMaxPh', Number(cfg.naohMaxPh ?? 8.45).toFixed(2));

  const state = document.getElementById('mode7SplitState');
  if (state) {
    const light = cfg.lightsActive === true ? 'Lights ON' : (cfg.lightsActive === false ? 'Lights OFF' : 'Light state --');
    state.textContent = (cfg.enabled === false ? 'Disabled' : 'Enabled') + ' • ' + light;
  }
}

function readPctInput(id, fallback){
  const n = Number(gv(id));
  if (!Number.isFinite(n) || n < 0 || n > 100) throw new Error('Enter 0-100 for ' + id);
  return n;
}

// Removed 2026-08-02: saveMode7DayNightSplit -- provably orphaned (zero
// remaining callers) after the Mode 7 Day/Night Alk Split card was removed.

function uiToggleLights() {
  const isApex = gv('lightSrc') == "1";
  document.getElementById('lightTimerGroup').style.display = isApex ? 'none' : 'block';
  document.getElementById('lightApexGroup').style.display = isApex ? 'block' : 'none';
}
  async function toggleEmergency(){
    await api('/api/emergency-stop','POST',{});
    await loadAll();
  }

  

  async function resetWiFi(){
    if(!confirm('Reset WiFi/provisioning and reboot the controller?')) return;
    await api('/api/reset-wifi','POST',{});
  }

let paramsChart = null;
let dosingChart = null;
let planChart = null;
let dosingHistoryChart = null;
let lastDosingHistoryLoadMs = 0;
const REALTIME_RETENTION_MS = 30 * 24 * 60 * 60 * 1000; // keep about 30 days in browser memory
const REALTIME_MAX_POINTS = 525000; // safety cap: about 30 days at 5-second refresh
const realtimeTimestamps = [];
const realtimeLabels = [];
const realtimeParams = { ph: [], alk: [], ca: [], mg: [], temp: [], ppt: [] };
const realtimeBuckets = { p1: [], p2: [], p3: [], p4: [] };
const realtimePlan = { kalk: [], afr: [], alk: [], ca: [], cacl2: [], naoh: [], mg: [] };
let activePlanChartKeys = [];
let activeBucketChartMode = null;

function cleanPumpLabel(name){
  return String(name || '').replace(/ \(Pump \d\)/, '');
}

function rebuildBucketChartForMode(mode){
  if (!dosingChart) return;
  const cfg = getModeCfg(Number(mode || currentDosingMode || 1));
  const modeKey = String(Number(mode || currentDosingMode || 1));
  if (activeBucketChartMode === modeKey) return;

  activeBucketChartMode = modeKey;
  for (let i = 0; i < 4; i++) {
    const pump = cfg.pumps.find(p => p.index === i);
    if (dosingChart.data.datasets[i]) {
      dosingChart.data.datasets[i].label = pump
        ? cleanPumpLabel(pump.name) + ' Bucket'
        : 'P' + (i + 1) + ' Unused Bucket';
    }
  }
}

function rebuildPlanChartForMode(mode){
  if (!planChart) return;
  const cfg = getModeCfg(Number(mode || currentDosingMode || 1));
  const nextKeys = cfg.pumps.map(p => p.key);
  if (activePlanChartKeys.join('|') === nextKeys.join('|')) return;

  activePlanChartKeys = nextKeys;
  planChart.data.datasets = cfg.pumps.map(p => ({
    label: cleanPumpLabel(p.name),
    data: realtimePlan[p.key] || [],
    tension: 0.25
  }));
  const meta = document.getElementById('realtimeChartMeta');
  if (meta) meta.textContent = 'AI plan graph showing active chemicals for ' + cfg.title;
}

function numOrNull(v){
  const n = Number(v);
  return Number.isFinite(n) ? n : null;
}

function shiftRealtimeRow(){
  realtimeTimestamps.shift();
  realtimeLabels.shift();
  Object.values(realtimeParams).forEach(arr => arr.shift());
  Object.values(realtimeBuckets).forEach(arr => arr.shift());
  Object.values(realtimePlan).forEach(arr => arr.shift());
}

function purgeOldRealtimePoints(nowMs){
  const cutoff = nowMs - REALTIME_RETENTION_MS;
  while (realtimeTimestamps.length && realtimeTimestamps[0] < cutoff) {
    shiftRealtimeRow();
  }
  while (realtimeTimestamps.length > REALTIME_MAX_POINTS) {
    shiftRealtimeRow();
  }
}

function pushRealtimeValue(arr, value){
  arr.push(value);
}

function chartStatus(msg){
  const box = document.getElementById('syncBox');
  if (box) box.textContent = msg;
  const meta = document.getElementById('realtimeChartMeta');
  if (meta) meta.textContent = msg;
}

function ensureRealtimeCharts(){
  if (typeof Chart === 'undefined') {
    chartStatus('Charts need Chart.js / internet access');
    return false;
  }
  if (paramsChart && dosingChart && planChart) return true;

  const ctxParams = document.getElementById('paramsChart')?.getContext('2d');
  const ctxDose = document.getElementById('dosingChart')?.getContext('2d');
  const ctxPlan = document.getElementById('planChart')?.getContext('2d');
  if (!ctxParams || !ctxDose || !ctxPlan) return false;

  paramsChart = new Chart(ctxParams, {
    type: 'line',
    data: {
      labels: realtimeLabels,
      datasets: [
        { label: 'Alk',  data: realtimeParams.alk,  tension: 0.25, yAxisID: 'y' },
        { label: 'pH',   data: realtimeParams.ph,   tension: 0.25, yAxisID: 'y1' },
        { label: 'Temp', data: realtimeParams.temp, tension: 0.25, yAxisID: 'y' },
        { label: 'Ca',   data: realtimeParams.ca,   tension: 0.25, yAxisID: 'y' },
        { label: 'Mg',   data: realtimeParams.mg,   tension: 0.25, yAxisID: 'y' },
        { label: 'PPT',  data: realtimeParams.ppt,  tension: 0.25, yAxisID: 'y' }
      ]
    },
    options: {
      animation: false,
      responsive: true,
      maintainAspectRatio: false,
      plugins: { legend: { labels: { color: '#eaf6f1' } } },
      scales: {
        x: { grid: { display: false }, ticks: { color: '#7fa6a0', maxTicksLimit: 8 } },
        y: { position: 'left', grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#7fa6a0' } },
        y1: { position: 'right', grid: { display: false }, ticks: { color: '#7fa6a0' }, min: 7.7, max: 8.7, title: { display: true, text: 'pH', color: '#7fa6a0' } }
      }
    }
  });

  dosingChart = new Chart(ctxDose, {
    type: 'line',
    data: {
      labels: realtimeLabels,
      datasets: [
        { label: 'P1 Bucket', data: realtimeBuckets.p1, tension: 0.25 },
        { label: 'P2 Bucket', data: realtimeBuckets.p2, tension: 0.25 },
        { label: 'P3 Bucket', data: realtimeBuckets.p3, tension: 0.25 },
        { label: 'P4 Bucket', data: realtimeBuckets.p4, tension: 0.25 }
      ]
    },
    options: {
      animation: false,
      responsive: true,
      maintainAspectRatio: false,
      plugins: { legend: { labels: { color: '#eaf6f1' } } },
      scales: {
        x: { grid: { display: false }, ticks: { color: '#7fa6a0', maxTicksLimit: 8 } },
        y: { grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#7fa6a0' }, beginAtZero: true }
      }
    }
  });

  planChart = new Chart(ctxPlan, {
    type: 'line',
    data: {
      labels: realtimeLabels,
      datasets: []
    },
    options: {
      animation: false,
      responsive: true,
      maintainAspectRatio: false,
      plugins: { legend: { labels: { color: '#eaf6f1' } } },
      scales: {
        x: { grid: { display: false }, ticks: { color: '#7fa6a0', maxTicksLimit: 8 } },
        y: { grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#7fa6a0' }, beginAtZero: true }
      }
    }
  });
  rebuildBucketChartForMode(currentDosingMode);
  rebuildPlanChartForMode(currentDosingMode);
  return true;
}

function bucketValue(s, pumpIndex, chemicalKey){
  const b = s?.buckets || s?.pendingMl || s?.pending || {};
  const aliases = {
    p1: ['p1','P1','pump1','0'],
    p2: ['p2','P2','pump2','1'],
    p3: ['p3','P3','pump3','2'],
    p4: ['p4','P4','pump4','3']
  };
  const physical = 'p' + (pumpIndex + 1);
  for (const k of aliases[physical]) {
    const n = numOrNull(b[k]);
    if (n !== null) return n;
  }
  const byChemical = numOrNull(b[chemicalKey]);
  return byChemical !== null ? byChemical : 0;
}

function addRealtimePoint(s){
  if (!ensureRealtimeCharts()) return;

  const now = new Date();
  const nowMs = now.getTime();
  const label = now.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit', second: '2-digit' });

  realtimeTimestamps.push(nowMs);
  realtimeLabels.push(label);
  pushRealtimeValue(realtimeParams.ph, numOrNull(s.ph));
  pushRealtimeValue(realtimeParams.alk, numOrNull(s.alk));
  pushRealtimeValue(realtimeParams.ca, numOrNull(s.ca));
  pushRealtimeValue(realtimeParams.mg, numOrNull(s.mg));
  pushRealtimeValue(realtimeParams.temp, numOrNull(s.temp ?? s.tempF));
  pushRealtimeValue(realtimeParams.ppt, numOrNull(s.ppt));

  const cfg = getModeCfg(Number(s.dosingMode ?? currentDosingMode ?? 1));
  for (let i = 0; i < 4; i++) {
    const pump = cfg.pumps.find(p => p.index === i);
    pushRealtimeValue(realtimeBuckets['p' + (i + 1)], pump ? bucketValue(s, i, pump.key) : 0);
  }

  const plan = planFromStatus(s);
  for (const k of Object.keys(realtimePlan)) {
    pushRealtimeValue(realtimePlan[k], planValue(plan, k));
  }

  purgeOldRealtimePoints(nowMs);
  rebuildBucketChartForMode(Number(s.dosingMode ?? currentDosingMode ?? 1));
  rebuildPlanChartForMode(Number(s.dosingMode ?? currentDosingMode ?? 1));

  paramsChart.update('none');
  dosingChart.update('none');
  planChart.update('none');
  chartStatus('Realtime charts: ' + label + ' • kept: ' + realtimeLabels.length + ' points / ~30 days max');
}

function formatHistoryDate(dateObj){
  return dateObj.toLocaleDateString([], { month: 'short', day: 'numeric' });
}

function ymdParts(dateObj){
  const y = dateObj.getFullYear();
  const m = String(dateObj.getMonth() + 1).padStart(2, '0');
  const d = String(dateObj.getDate()).padStart(2, '0');
  return { month: `${y}-${m}`, day: d, label: formatHistoryDate(dateObj) };
}

function historyValue(obj, keys){
  if (!obj) return 0;
  for (const k of keys) {
    const n = numOrNull(obj[k]);
    if (n !== null) return n;
  }
  return 0;
}

function pumpLabelForHistory(mode, pumpIndex){
  const cfg = getModeCfg(Number(mode || currentDosingMode || 1));
  const pump = cfg.pumps.find(p => p.index === pumpIndex);
  return pump ? cleanPumpLabel(pump.name) : ('P' + (pumpIndex + 1));
}

function historyRowFromRecord(label, record, mode){
  const dosing = record?.dosing || record || {};
  const p1 = historyValue(dosing, ['p1','P1','pump1','0','kalk','afr','alk']);
  const p2 = historyValue(dosing, ['p2','P2','pump2','1','cacl2','ca','afr','alk']);
  const p3 = historyValue(dosing, ['p3','P3','pump3','2','naoh','mg','ca']);
  const p4 = historyValue(dosing, ['p4','P4','pump4','3','mg','alk','naoh']);
  const total = historyValue(record?.params, ['totalDose']) || (p1 + p2 + p3 + p4);
  return { label, p1, p2, p3, p4, total, samples: Number(record?.sampleCount || 0), mode };
}

function ensureDosingHistoryChart(){
  if (typeof Chart === 'undefined') {
    const meta = document.getElementById('dosingHistoryMeta');
    if (meta) meta.textContent = 'History chart needs Chart.js / internet access';
    return false;
  }
  const ctx = document.getElementById('dosingHistoryChart')?.getContext('2d');
  if (!ctx) return false;
  if (dosingHistoryChart) return true;

  dosingHistoryChart = new Chart(ctx, {
    type: 'bar',
    data: { labels: [], datasets: [] },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: 'index', intersect: false },
      plugins: {
        legend: { position: 'top', labels: { color: '#eaf6f1', usePointStyle: true } },
        tooltip: {
          callbacks: {
            label: function(ctx){
              const v = Number(ctx.parsed.y || 0);
              return ctx.dataset.label + ': ' + v.toFixed(1) + ' mL';
            },
            footer: function(items){
              const row = dosingHistoryRows[items[0]?.dataIndex];
              return row ? ('Total actual: ' + row.total.toFixed(1) + ' mL') : '';
            }
          }
        }
      },
      scales: {
        x: {
          stacked: true,
          grid: { display: false },
          ticks: { color: '#7fa6a0', maxRotation: 0, autoSkip: true, maxTicksLimit: 12 },
          title: { display: true, text: 'Day', color: '#7fa6a0' }
        },
        y: {
          stacked: true,
          beginAtZero: true,
          grid: { color: 'rgba(255,255,255,0.06)' },
          ticks: { color: '#7fa6a0' },
          title: { display: true, text: 'Actual dosed mL/day', color: '#7fa6a0' }
        },
        yTotal: {
          position: 'right',
          beginAtZero: true,
          grid: { display: false },
          ticks: { color: '#7fa6a0' },
          title: { display: true, text: 'Total mL/day', color: '#7fa6a0' }
        }
      }
    }
  });
  return true;
}

let dosingHistoryRows = [];

function renderDosingHistory(rows, sourceText){
  if (!ensureDosingHistoryChart()) return;
  dosingHistoryRows = rows;
  const mode = Number(currentStatus.dosingMode || currentDosingMode || 1);
  const labels = rows.map(r => r.label);
  const total = rows.reduce((sum, r) => sum + r.total, 0);
  const avg = rows.length ? total / rows.length : 0;
  const last = rows.length ? rows[rows.length - 1].total : 0;
  const max = rows.reduce((m, r) => Math.max(m, r.total), 0);

  dosingHistoryChart.data.labels = labels;
  dosingHistoryChart.data.datasets = [
    { type: 'bar', label: pumpLabelForHistory(mode, 0), data: rows.map(r => r.p1), stack: 'actual', backgroundColor: 'rgba(255,107,74,0.75)', borderColor: '#ff6b4a', borderWidth: 1 },
    { type: 'bar', label: pumpLabelForHistory(mode, 1), data: rows.map(r => r.p2), stack: 'actual', backgroundColor: 'rgba(79,224,168,0.72)', borderColor: '#4fe0a8', borderWidth: 1 },
    { type: 'bar', label: pumpLabelForHistory(mode, 2), data: rows.map(r => r.p3), stack: 'actual', backgroundColor: 'rgba(255,182,72,0.72)', borderColor: '#ffb648', borderWidth: 1 },
    { type: 'bar', label: pumpLabelForHistory(mode, 3), data: rows.map(r => r.p4), stack: 'actual', backgroundColor: 'rgba(255,84,112,0.72)', borderColor: '#ff5470', borderWidth: 1 },
    { type: 'line', label: 'Total actual', data: rows.map(r => r.total), yAxisID: 'yTotal', tension: 0.25, borderColor: '#f4fbf8', backgroundColor: '#f4fbf8', pointRadius: 3, pointHoverRadius: 5, borderWidth: 2 }
  ];
  dosingHistoryChart.update();

  const meta = document.getElementById('dosingHistoryMeta');
  if (meta) meta.textContent = sourceText + ' • ' + rows.length + ' day(s)';
  const summary = document.getElementById('dosingHistorySummary');
  if (summary) {
    summary.innerHTML = `
      <div class="history-stat"><div class="k">Last day</div><div class="v">${last.toFixed(1)} mL</div></div>
      <div class="history-stat"><div class="k">Average/day</div><div class="v">${avg.toFixed(1)} mL</div></div>
      <div class="history-stat"><div class="k">Max day</div><div class="v">${max.toFixed(1)} mL</div></div>
      <div class="history-stat"><div class="k">Range total</div><div class="v">${total.toFixed(1)} mL</div></div>`;
  }
}

async function loadLocalDosingHistoryToday(){
  const h = await api('/api/history');
  const label = (h.labels && h.labels[0]) || 'Today';
  const todayRecord = {
    dosing: h.dosing?.[label] || h.dosing?.Today || {},
    params: h.params?.[label] || h.params?.Today || {},
    sampleCount: h.sampleCount || 0
  };
  return [historyRowFromRecord(label, todayRecord, Number(h.dosingMode || currentDosingMode || 1))];
}

async function loadFirebaseDosingHistory(days){
  if (!initFirebaseAppForDatabase()) return [];
  const deviceId = currentStatus.deviceId || 'reefDoser2';
  const end = new Date();
  const start = new Date(end.getTime() - (days - 1) * 24 * 60 * 60 * 1000);
  const months = [];
  const cursor = new Date(start.getFullYear(), start.getMonth(), 1);
  while (cursor <= end) {
    months.push(`${cursor.getFullYear()}-${String(cursor.getMonth() + 1).padStart(2, '0')}`);
    cursor.setMonth(cursor.getMonth() + 1);
  }

  const monthData = {};
  await Promise.all(months.map(async month => {
    try {
      const snap = await firebase.database().ref('/devices/' + deviceId + '/history/' + month).once('value');
      monthData[month] = snap.val() || {};
    } catch (err) {
      console.warn('History read failed for ' + month, err);
      monthData[month] = {};
    }
  }));

  const rows = [];
  for (let i = 0; i < days; i++) {
    const d = new Date(start.getTime() + i * 24 * 60 * 60 * 1000);
    const part = ymdParts(d);
    const rec = monthData[part.month]?.[part.day];
    if (rec) rows.push(historyRowFromRecord(part.label, rec, Number(rec.dosingMode || currentDosingMode || 1)));
  }
  return rows;
}

async function loadDosingHistory(force=false){
  const now = Date.now();
  if (!force && now - lastDosingHistoryLoadMs < 5 * 60 * 1000) return;
  lastDosingHistoryLoadMs = now;

  const rangeEl = document.getElementById('historyRange');
  const days = Math.max(1, Number(rangeEl?.value || 30));
  const meta = document.getElementById('dosingHistoryMeta');
  if (meta) meta.textContent = 'Loading actual dosing history...';

  let rows = [];
  try { rows = await loadFirebaseDosingHistory(days); } catch (err) { console.warn(err); }

  try {
    const todayRows = await loadLocalDosingHistoryToday();
    if (todayRows.length) {
      const todayLabel = todayRows[0].label;
      const existingIdx = rows.findIndex(r => r.label === todayLabel || r.label === formatHistoryDate(new Date()));
      if (existingIdx >= 0) rows[existingIdx] = todayRows[0];
      else rows.push(todayRows[0]);
    }
  } catch (err) { console.warn('Local history read failed', err); }

  rows = rows.filter(r => r && Number.isFinite(r.total)).slice(-days);
  if (!rows.length) rows = [{ label: 'No data', p1: 0, p2: 0, p3: 0, p4: 0, total: 0, samples: 0, mode: currentDosingMode }];
  const source = rows.length > 1 ? 'Firebase daily records + live today' : 'Live controller today only';
  renderDosingHistory(rows, source);
}

function loadLocalReport(){
  loadDosingHistory(true);
}

    window.addEventListener('load', () => {
      populateHours();
      setTimeout(() => { ensureRealtimeCharts(); loadDosingHistory(true); }, 500);
    });

  setInterval(loadAll, 5000);
  loadAll();
  // §5 free chemical declaration: loaded once at startup, not on the
  // 5-second poll -- this list only changes via explicit add/edit/remove,
  // which already call loadDeclaredChemicals() themselves after committing.
  loadChemicalPresets();
  loadDeclaredChemicals();









  document.addEventListener('DOMContentLoaded', function(){
    setTimeout(function(){
      setRecipeUnitLabels();
      updateRecipePreview();
    }, 500);
  });

</script>








</body>
</html>
)HTML";