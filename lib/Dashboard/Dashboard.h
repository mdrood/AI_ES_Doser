constexpr char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AIDoser | Local Dashboard</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    :root{
      --bg:#020817;
      --bg-soft:#0f172a;
      --card:rgba(15,23,42,.88);
      --card-2:rgba(30,41,59,.72);
      --border:rgba(148,163,184,.20);
      --text:#e2e8f0;
      --muted:#94a3b8;
      --accent:#22d3ee;
      --accent-2:#38bdf8;
      --success:#4ade80;
      --danger:#f87171;
      --warning:#fbbf24;
      --shadow:0 16px 50px rgba(0,0,0,.35);
    }
    *{box-sizing:border-box}
    body{
      margin:0;
      min-height:100vh;
      font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
      color:var(--text);
      background:
        radial-gradient(circle at top left, rgba(14,165,233,.38), transparent 34%),
        radial-gradient(circle at top right, rgba(34,211,238,.18), transparent 26%),
        linear-gradient(180deg,#020617 0%,#020817 100%);
      background-attachment:fixed;
      padding:18px;
    }
    .shell{width:100%;max-width:1220px;margin:0 auto}
    .hero{
      display:flex;justify-content:space-between;align-items:center;gap:18px;
      padding:24px;border:1px solid var(--border);border-radius:24px;
      background:linear-gradient(180deg, rgba(15,23,42,.95), rgba(15,23,42,.78));
      box-shadow:var(--shadow);backdrop-filter:blur(14px);margin-bottom:18px;
    }
    .hero-left{display:flex;gap:16px;align-items:center}
    .logo{
      width:58px;height:58px;border-radius:18px;
      background:linear-gradient(135deg,#22d3ee,#2563eb);
      box-shadow:0 12px 30px rgba(34,211,238,.25);
      position:relative;overflow:hidden;
    }
    .logo:before,.logo:after{
      content:"";position:absolute;border:2px solid rgba(255,255,255,.9);border-radius:999px;
    }
    .logo:before{width:34px;height:34px;left:10px;top:11px;border-top-color:transparent;border-left-color:transparent;transform:rotate(28deg)}
    .logo:after{width:18px;height:18px;right:8px;bottom:8px;border-top-color:transparent;border-right-color:transparent;opacity:.8}
    h1{margin:0;font-size:1.8rem;letter-spacing:-.02em}
    .sub{color:var(--muted);margin-top:4px;font-size:.95rem}
    .hero-right{display:flex;gap:10px;flex-wrap:wrap;justify-content:flex-end}
    .chip{
      display:inline-flex;align-items:center;gap:8px;
      padding:10px 14px;border-radius:999px;border:1px solid var(--border);
      background:rgba(2,6,23,.38);color:var(--muted);font-weight:700;font-size:.82rem
    }
    .dot{width:8px;height:8px;border-radius:50%;background:var(--accent);box-shadow:0 0 10px rgba(34,211,238,.65)}
    .chips{
      display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:12px;margin-bottom:18px;
    }
    .sensor{
      border:1px solid var(--border);border-radius:18px;padding:16px;
      background:linear-gradient(180deg, rgba(15,23,42,.95), rgba(15,23,42,.72));
      box-shadow:var(--shadow);
    }
    .sensor .label{color:var(--muted);font-size:.78rem;text-transform:uppercase;letter-spacing:.12em}
    .sensor .value{font-size:1.55rem;font-weight:800;margin-top:8px;color:#f8fafc}
    .sensor .unit{font-size:.92rem;color:var(--accent);margin-left:4px}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(340px,1fr));gap:18px}
    .card{
      border:1px solid var(--border);border-radius:20px;padding:20px;
      background:linear-gradient(180deg, rgba(15,23,42,.92), rgba(15,23,42,.76));
      box-shadow:var(--shadow);backdrop-filter:blur(12px);
    }
    .card-title{
      display:flex;justify-content:space-between;align-items:center;gap:12px;
      margin-bottom:16px
    }
    .card-title h3{
      margin:0;color:var(--accent);font-size:.88rem;text-transform:uppercase;letter-spacing:.14em
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
      background:rgba(2,6,23,.45);color:var(--text);
      padding:12px 13px;outline:none
    }
    input:focus,select:focus{border-color:rgba(34,211,238,.55);box-shadow:0 0 0 3px rgba(34,211,238,.12)}
    button{
      border:none;padding:12px 14px;font-weight:800;cursor:pointer;transition:.18s ease;
      background:linear-gradient(135deg,var(--accent),var(--accent-2));color:#03111f;
    }
    button:hover{transform:translateY(-1px);filter:brightness(1.04)}
    button.sec{
      background:transparent;color:var(--accent);border:1px solid rgba(34,211,238,.34)
    }
    button.soft{
      background:rgba(30,41,59,.9);color:var(--text);border:1px solid var(--border)
    }
    button.danger{
      background:linear-gradient(135deg,#fb7185,#dc2626);color:white
    }
    .mode-btn.active,
    .pill-btn.active{
      background:linear-gradient(135deg, rgba(34,211,238,.25), rgba(56,189,248,.18));
      color:var(--accent);border:1px solid rgba(34,211,238,.45)
    }
    .mode-btn,.pill-btn{
      background:rgba(30,41,59,.85);color:var(--muted);border:1px solid var(--border)
    }
    .stack{display:flex;flex-direction:column;gap:12px}
    .line{display:flex;justify-content:space-between;gap:10px;align-items:center}
    .line .k{color:var(--muted);font-size:.84rem}
    .line .v{font-weight:800}
    .help{font-size:.78rem;color:var(--muted);line-height:1.5}
    .status-pill{
      display:inline-flex;align-items:center;gap:8px;padding:8px 12px;
      border-radius:999px;border:1px solid var(--border);background:rgba(2,6,23,.35);
      font-size:.8rem;font-weight:700;color:var(--muted)
    }
    .cal-list{display:flex;flex-direction:column;gap:12px}
    .cal-item{
      border:1px solid rgba(148,163,184,.16);background:rgba(2,6,23,.28);
      border-radius:16px;padding:14px
    }
    .cal-top{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:10px}
    .cal-name{font-weight:900}
    .cal-sub{font-size:.76rem;color:var(--muted);margin-top:2px}
    .flow-badge{
      padding:8px 10px;border-radius:12px;background:rgba(34,211,238,.10);
      border:1px solid rgba(34,211,238,.22);font-size:.82rem;font-weight:800;color:var(--accent)
    }
    .footer-note{margin-top:14px;font-size:.75rem;color:var(--muted)}
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
  <div class="card-title"><h3>System Geometry</h3></div>
  <div class="line">
    <div class="k">Tank Volume (Gallons)</div>
    <input type="number" id="tankGal" step="0.1" placeholder="e.g. 120" style="width:100px">
  </div>
  <button class="sec" onclick="saveVol()">Update Volume</button>
</div>
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
        <div class="footer-note">This is your local operating mode. It is separate from the 1–6 dosing implementation below.</div>
      </div>

      <div class="card">
        <div class="card-title">
          <h3>Dosing Implementation</h3>
          <span class="status-pill" id="doseModePill">Mode 1</span>
        </div>
        <select id="dosingModeSelect">
          <option value="1">Mode 1: Kalk (Pump 1)</option>
          <option value="2">Mode 2: AFR (Pump 1)</option>
          <option value="3">Mode 3: Kalk + AFR + Mg (P1–P3)</option>
          <option value="4">Mode 4: Alk + Ca + Mg (P1–P3)</option>
          <option value="5">Mode 5: Kalk + Alk + Ca + Mg (P1–P4)</option>
          <option value="6">Mode 6: Kalk + CaCl2 + NaOH + Mg (P1–P4)</option>
        </select>
        <button onclick="saveDosingMode()">Save Dosing Implementation</button>
        <div class="help" id="doseModeHelp">Pump mapping will update immediately below.</div>
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
        <button onclick="saveTest()">Update AI Dosing Plan</button>
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
      <div class="card" style="grid-column:1 / -1">
        <div class="card-title">
          <h3>Flow Calibration</h3>
          <span class="meta">Only pumps used by the selected dosing implementation are shown</span>
        </div>
        <div class="cal-list" id="calibrationContainer"></div>
        <div class="footer-note">These fields save each pump’s flow in mL/min locally. That keeps the dashboard and ESP32 aligned without removing your working dose logic.</div>
      </div>
    </section>
  </div>
  <div class="card">
  <div class="card-title">
    <h3>Dosing Safeties</h3>
    <span class="meta">AI Logic Constraints</span>
  </div>
  <div class="two">
    <div>
      <div class="help" style="margin-bottom:8px;">Frequency Threshold (mL)</div>
      <input type="number" id="dTresh" step="0.1" min="0.1" placeholder="e.g. 25" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">
    </div>
    <div>
      <div class="help" style="margin-bottom:8px;">Max Hourly Safety (mL)</div>
      <input type="number" id="dMax" step="1" min="1" placeholder="e.g. 1500" onfocus="markSafetyDirty()" oninput="markSafetyDirty()">
    </div>
  </div>
  <button class="sec" onclick="saveDosingSafeties()" style="margin-top:12px">Update Safety Rails</button>
  <div class="footer-note">Threshold: accumulation required to fire pumps. Max: hardware safety cutoff.</div>
</div>

<div class="card">
  <div class="card-title">
    <h3>AI Baseline Demand</h3>
    <span class="meta">Known daily dosing + AI correction</span>
  </div>
  <div class="two">
    <div>
      <div class="help" style="margin-bottom:8px;">Coral Load</div>
      <select id="coralLoad" onchange="markAiBaselineDirty()">
        <option value="light">Light</option>
        <option value="mixed">Mixed Reef</option>
        <option value="heavy">Heavy Mixed</option>
        <option value="sps-heavy">SPS-heavy</option>
        <option value="custom">Custom</option>
      </select>
    </div>
    <div>
      <div class="help" style="margin-bottom:8px;">Tank Volume Used</div>
      <input type="text" id="baselineTankHint" value="-- gallons" readonly>
    </div>
  </div>
  <div class="two" style="margin-top:12px;">
    <div>
      <div class="help" style="margin-bottom:8px;">Kalk Baseline (mL/day)</div>
      <input type="number" id="baseKalk" step="1" min="0" placeholder="e.g. 26500" onfocus="markAiBaselineDirty()" oninput="markAiBaselineDirty()">
    </div>
    <div>
      <div class="help" style="margin-bottom:8px;">CaCl2 Baseline (mL/day)</div>
      <input type="number" id="baseCacl2" step="1" min="0" placeholder="e.g. 500" onfocus="markAiBaselineDirty()" oninput="markAiBaselineDirty()">
    </div>
    <div>
      <div class="help" style="margin-bottom:8px;">NaOH Baseline (mL/day)</div>
      <input type="number" id="baseNaoh" step="1" min="0" placeholder="e.g. 500" onfocus="markAiBaselineDirty()" oninput="markAiBaselineDirty()">
    </div>
    <div>
      <div class="help" style="margin-bottom:8px;">Mg Baseline (mL/day)</div>
      <input type="number" id="baseMg" step="1" min="0" placeholder="e.g. 0" onfocus="markAiBaselineDirty()" oninput="markAiBaselineDirty()">
    </div>
  </div>
  <div class="two" style="margin-top:12px;">
    <button class="sec" onclick="estimateAiBaseline()">Estimate From Load + Volume</button>
    <button class="sec" onclick="saveAiBaseline()">Save AI Baseline</button>
  </div>
  <div class="footer-note">Use this for large reef systems: enter the dosing the tank already consumes each day. The AI then adjusts up/down from this baseline instead of guessing total demand from zero.</div>
</div>

<div class="card" style="grid-column: 1 / -1">
  <div class="card-title">
    <h3>Monthly Analytics</h3>
    <input type="month" id="repMonth" onchange="loadLocalReport()">
  </div>
  
  <div style="margin-bottom: 30px;">
    <h4 style="color:var(--accent); font-size: 0.75rem; text-transform: uppercase; margin-bottom:10px;">Water Parameters (Daily Avg)</h4>
    <div style="height:280px;"><canvas id="paramsChart"></canvas></div>
  </div>

  <div>
    <h4 style="color:var(--accent); font-size: 0.75rem; text-transform: uppercase; margin-bottom:10px;">Daily Dosing Totals (mL)</h4>
    <div style="height:250px;"><canvas id="dosingChart"></canvas></div>
  </div>
</div>

<script>
  const DOSING_MODES = {
    1: {
      title: "Mode 1 • Kalk",
      pumps: [{index:0,key:"kalk",name:"Kalk (Pump 1)"}]
    },
    2: {
      title: "Mode 2 • AFR",
      pumps: [{index:0,key:"afr",name:"AFR (Pump 1)"}]
    },
    3: {
      title: "Mode 3 • Kalk + AFR + Mg",
      pumps: [
        {index:0,key:"kalk",name:"Kalk (Pump 1)"},
        {index:1,key:"afr",name:"AFR (Pump 2)"},
        {index:2,key:"mg",name:"Mg (Pump 3)"}
      ]
    },
    4: {
      title: "Mode 4 • Alk + Ca + Mg",
      pumps: [
        {index:0,key:"alk",name:"Alk (Pump 1)"},
        {index:1,key:"ca",name:"Calcium (Pump 2)"},
        {index:2,key:"mg",name:"Mg (Pump 3)"}
      ]
    },
    5: {
      title: "Mode 5 • Kalk + Alk + Ca + Mg",
      pumps: [
        {index:0,key:"kalk",name:"Kalk (Pump 1)"},
        {index:1,key:"alk",name:"Alk (Pump 2)"},
        {index:2,key:"ca",name:"Calcium (Pump 3)"},
        {index:3,key:"mg",name:"Mg (Pump 4)"}
      ]
    },
    6: {
      title: "Mode 6 • Kalk + CaCl2 + NaOH + Mg",
      pumps: [
        {index:0,key:"kalk",name:"Kalk (Pump 1)"},
        {index:1,key:"cacl2",name:"CaCl2 (Pump 2)"},
        {index:2,key:"naoh",name:"NaOH (Pump 3)"},
        {index:3,key:"mg",name:"Mg (Pump 4)"}
      ]
    }
  };

  let currentStatus = {};
  let currentMode = 1;
  let currentDosingMode = 1;
  let lightDirty = false;

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
    return safetyDirty || activeId === 'dTresh' || activeId === 'dMax';
  }

  let aiBaselineDirty = false;
  function markAiBaselineDirty(){ aiBaselineDirty = true; }
  function clearAiBaselineDirty(){ aiBaselineDirty = false; }
  function isAiBaselineEditing(){
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';
    return aiBaselineDirty || activeId === 'coralLoad' || activeId === 'baseKalk' || activeId === 'baseCacl2' || activeId === 'baseNaoh' || activeId === 'baseMg';
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

  function getModeCfg(mode){
    return DOSING_MODES[Number(mode)] || DOSING_MODES[1];
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
  alert("System volume set to " + gal.toFixed(1) + " Gallons");
}

  function renderPumpSelect(mode){
    const sel = document.getElementById('pSel');
    if (!sel) return;

    const cfg = getModeCfg(mode);
    const previousValue = sel.value;
    const activeId = document.activeElement && document.activeElement.id ? document.activeElement.id : '';

    // Do not rebuild the quick-dose pump dropdown while the user is changing it.
    // Rebuilding innerHTML every 5-second refresh forces the select back to Pump 1.
    if (activeId === 'pSel') return;

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
      const flow = Number((flows || {})[p.key]);
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

  function renderStatus(s){
    currentStatus = s || {};
    currentMode = Number(s.mode ?? 1);
    currentDosingMode = Number(s.dosingMode ?? 1);

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

    document.getElementById('dosingModeSelect').value = String(currentDosingMode);
    const modeCfg = getModeCfg(currentDosingMode);
    document.getElementById('doseModePill').textContent = modeCfg.title;
    document.getElementById('doseModeHelp').textContent = `${modeCfg.pumps.length} active pump(s): ${modeCfg.pumps.map(p => p.name).join(', ')}`;

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
    document.getElementById('onlineDot').style.boxShadow = s.wifiConnected ? '0 0 10px rgba(74,222,128,.8)' : '0 0 10px rgba(248,113,113,.8)';
    document.getElementById('syncBox').textContent = 'Last local sync: ' + new Date().toLocaleTimeString();
    // Do not overwrite safety inputs while the user is changing them.
    const dTreshEl = document.getElementById('dTresh');
    const dMaxEl = document.getElementById('dMax');
    if (!isSafetyEditing()) {
      if (dTreshEl) dTreshEl.value = s.dosingThreshold || 1.0;
      if (dMaxEl) dMaxEl.value = s.maxHourlyLimit || 15;
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

    const tankInput = document.getElementById('tankGal');
    if (tankInput && document.activeElement !== tankInput) {
      const gal = Number(s.tankGallons ?? s.tankGal ?? s.gallons);
      if (Number.isFinite(gal) && gal > 0) tankInput.value = gal.toFixed(1);
    }
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



  async function loadAll(){
    
    try{
      const s = await api('/api/status');
      renderStatus(s);
    }catch(err){
      document.getElementById('syncBox').textContent = 'OFFLINE';
      console.error(err);
    }
  }

  async function setMode(mode){
    await api('/api/mode', 'POST', { mode });
    await loadAll();
  }

  async function saveDosingMode(){
    const dosingMode = parseInt(document.getElementById('dosingModeSelect').value, 10);
    await api('/api/dosing-mode', 'POST', { dosingMode });
    await loadAll();
    alert('Dosing implementation saved.');
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
    await api('/api/manual-test','POST',{
      alk: parseFloat(document.getElementById('alk').value),
      ca: parseFloat(document.getElementById('ca').value),
      mg: parseFloat(document.getElementById('mg').value),
      ph: parseFloat(document.getElementById('ph').value)
    });
    await loadAll();
    alert('AI recalculated.');
  }

  async function liveDose(){
    const pumpIndex = parseInt(document.getElementById('pSel').value, 10);
    const ml = parseFloat(document.getElementById('v_ml').value);
    if(!Number.isFinite(ml) || ml <= 0){
      alert('Enter a valid dose amount in mL.');
      return;
    }
    await api('/api/live-dose','POST',{ pumpIndex, ml });
    alert('Dose command sent.');
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


function estimateAiBaseline() {
  const gal = Number(currentStatus.tankGallons ?? document.getElementById('tankGal')?.value ?? 0);
  const load = gv('coralLoad') || 'mixed';
  if (!Number.isFinite(gal) || gal <= 0) {
    alert('Set tank volume first so the dashboard can estimate baseline demand.');
    return;
  }

  // Educated starting points in mL per gallon per day. These are only prefill
  // guesses; the saved values remain editable and become the AI baseline.
  const factors = {
    'light':     { kalk: 4.0,  cacl2: 0.05, naoh: 0.05, mg: 0.00 },
    'mixed':     { kalk: 8.0,  cacl2: 0.12, naoh: 0.12, mg: 0.01 },
    'heavy':     { kalk: 15.0, cacl2: 0.25, naoh: 0.25, mg: 0.02 },
    'sps-heavy': { kalk: 24.1, cacl2: 0.45, naoh: 0.45, mg: 0.00 },
    'custom':    { kalk: 8.0,  cacl2: 0.12, naoh: 0.12, mg: 0.01 }
  };
  const f = factors[load] || factors.mixed;

  document.getElementById('baseKalk').value = Math.round(gal * f.kalk);
  document.getElementById('baseCacl2').value = Math.round(gal * f.cacl2);
  document.getElementById('baseNaoh').value = Math.round(gal * f.naoh);
  document.getElementById('baseMg').value = Math.round(gal * f.mg);
  markAiBaselineDirty();
}

async function saveAiBaseline() {
  const payload = {
    coralLoad: gv('coralLoad') || 'custom',
    kalk: parseFloat(gv('baseKalk')),
    cacl2: parseFloat(gv('baseCacl2')),
    naoh: parseFloat(gv('baseNaoh')),
    mg: parseFloat(gv('baseMg'))
  };

  for (const key of ['kalk','cacl2','naoh','mg']) {
    if (!Number.isFinite(payload[key]) || payload[key] < 0) {
      alert('Enter a valid non-negative mL/day baseline for ' + key + '.');
      return;
    }
  }

  const res = await api('/api/config/ai-baseline', 'POST', payload);
  if (res && res.ok === false) {
    alert('AI baseline save failed: ' + (res.error || res.raw || 'unknown error'));
    return;
  }

  currentStatus.aiBaseline = payload;
  clearAiBaselineDirty();
  await loadAll();
  alert('AI baseline demand saved.');
}

async function saveDosingSafeties() {
  const threshold = parseFloat(gv('dTresh'));
  const maxLimit = parseFloat(gv('dMax'));

  if (!Number.isFinite(threshold) || threshold <= 0) {
    alert('Enter a valid Frequency Threshold in mL.');
    return;
  }
  if (!Number.isFinite(maxLimit) || maxLimit <= 0) {
    alert('Enter a valid Max Hourly Safety in mL.');
    return;
  }

  const payload = { threshold, maxLimit };
  const res = await api('/api/config/safeties', 'POST', payload);
  if (res && res.ok === false) {
    alert('Safety save failed: ' + (res.error || res.raw || 'unknown error'));
    return;
  }

  // Update local copy immediately so the next refresh displays what was saved.
  currentStatus.dosingThreshold = threshold;
  currentStatus.maxHourlyLimit = maxLimit;
  clearSafetyDirty();

  const dTreshEl = document.getElementById('dTresh');
  const dMaxEl = document.getElementById('dMax');
  if (dTreshEl) dTreshEl.value = threshold;
  if (dMaxEl) dMaxEl.value = maxLimit;

  await loadAll();
  alert("Safety Rails Updated");
}

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

let paramsChart, dosingChart;

function numOrNull(v){
  const n = Number(v);
  return Number.isFinite(n) ? n : null;
}

function destroyCharts(){
  if (paramsChart) { paramsChart.destroy(); paramsChart = null; }
  if (dosingChart) { dosingChart.destroy(); dosingChart = null; }
}

function chartStatus(msg){
  const box = document.getElementById('syncBox');
  if (box) box.textContent = msg;
}

async function loadLocalReport() {
  try {
    if (typeof Chart === 'undefined') {
      chartStatus('Charts need Chart.js / internet access');
      console.error('Chart.js did not load. The ESP32 page uses the Chart.js CDN, so the browser needs internet access unless Chart.js is bundled locally.');
      return;
    }

    // Standalone fix: read graph data from the ESP32 local endpoint, not Firebase.
    const res = await fetch('/api/history', { cache: 'no-store' });
    if (!res.ok) throw new Error('/api/history HTTP ' + res.status);
    const data = await res.json();

    const rawLabels = Array.isArray(data.labels) && data.labels.length
      ? data.labels
      : Object.keys(data.params || {}).sort();

    const labels = rawLabels.length ? rawLabels : ['Today'];
    const params = data.params || {};
    const dosing = data.dosing || {};

    function row(label){
      return params[label] || params[String(label)] || {};
    }

    const totalDoseSeries = labels.map(d => {
      const r = row(d);
      const fromParams = numOrNull(r.totalDose);
      if (fromParams !== null) return fromParams;
      const dk = dosing[d] || dosing[String(d)] || dosing;
      return ['kalk','afr','alk','ca','cacl2','naoh','mg','tbd','p1','p2','p3','p4']
        .reduce((sum, key) => sum + (numOrNull(dk[key]) || 0), 0);
    });

    const ctxParams = document.getElementById('paramsChart').getContext('2d');
    const ctxDose = document.getElementById('dosingChart').getContext('2d');
    destroyCharts();

    paramsChart = new Chart(ctxParams, {
      type: 'line',
      data: {
        labels,
        datasets: [
          { label: 'Alk',  data: labels.map(d => numOrNull(row(d).alk)),  tension: 0.3, yAxisID: 'y' },
          { label: 'pH',   data: labels.map(d => numOrNull(row(d).ph)),   tension: 0.3, yAxisID: 'y1' },
          { label: 'Temp', data: labels.map(d => numOrNull(row(d).temp ?? row(d).tempF)), tension: 0.3, yAxisID: 'y' },
          { label: 'Ca',   data: labels.map(d => numOrNull(row(d).ca)),   tension: 0.3, yAxisID: 'y' },
          { label: 'Mg',   data: labels.map(d => numOrNull(row(d).mg)),   tension: 0.3, yAxisID: 'y' },
          { label: 'PPT',  data: labels.map(d => numOrNull(row(d).ppt ?? row(d).sal)), tension: 0.3, yAxisID: 'y' }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { labels: { color: '#e2e8f0' } } },
        scales: {
          x: { grid: { display: false }, ticks: { color: '#94a3b8' } },
          y: { position: 'left', grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#94a3b8' } },
          y1: { position: 'right', grid: { display: false }, ticks: { color: '#94a3b8' }, min: 7.7, max: 8.6, title: { display: true, text: 'pH Scale', color: '#94a3b8' } }
        }
      }
    });

    dosingChart = new Chart(ctxDose, {
      type: 'bar',
      data: {
        labels,
        datasets: [{
          label: 'Total Daily mL',
          data: totalDoseSeries,
          borderWidth: 1
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { display: false } },
        scales: {
          x: { grid: { display: false }, ticks: { color: '#94a3b8' } },
          y: { grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#94a3b8' }, beginAtZero: true }
        }
      }
    });

    chartStatus('Charts updated: ' + new Date().toLocaleTimeString());
  } catch (e) {
    destroyCharts();
    chartStatus('Chart load failed');
    console.error('Chart Load Error:', e);
  }
}

    window.addEventListener('load', () => {
      populateHours();
      const d = new Date();
      const monthStr = d.toISOString().slice(0, 7);
      document.getElementById('repMonth').value = monthStr;
      setTimeout(loadLocalReport, 1000);
    });

  setInterval(loadAll, 5000);
  loadAll();
</script>
</body>
</html>
)HTML";
