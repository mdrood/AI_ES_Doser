constexpr char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AIDoser | Local Dashboard</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <!-- Firebase SDKs are only used when this dashboard is served from HTTPS Firebase Hosting. -->
  <script src="https://www.gstatic.com/firebasejs/10.12.2/firebase-app-compat.js"></script>
  <script src="https://www.gstatic.com/firebasejs/10.12.2/firebase-database-compat.js"></script>
  <script src="https://www.gstatic.com/firebasejs/10.12.2/firebase-messaging-compat.js"></script>
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

    .plan-list{display:flex;flex-direction:column;gap:10px}
    .plan-row{
      display:flex;align-items:center;justify-content:space-between;gap:12px;
      padding:12px 14px;border-radius:16px;border:1px solid rgba(148,163,184,.18);
      background:rgba(2,6,23,.30)
    }
    .plan-left{display:flex;align-items:center;gap:10px;min-width:0}
    .plan-name{font-weight:900;color:#f8fafc}
    .plan-sub{font-size:.75rem;color:var(--muted);margin-top:2px}
    .plan-amt{font-weight:900;color:var(--accent);font-size:1.05rem;white-space:nowrap}
    .chart-stack{display:grid;grid-template-columns:1fr;gap:26px}
    .footer-note{margin-top:14px;font-size:.75rem;color:var(--muted)}
    .chem-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}
    .chem-item{border:1px solid rgba(148,163,184,.16);background:rgba(2,6,23,.28);border-radius:16px;padding:14px}
    .chem-top{display:flex;justify-content:space-between;gap:10px;align-items:flex-start;margin-bottom:10px}
    .chem-name{font-weight:900}
    .chem-left{font-size:.85rem;color:var(--muted);margin-top:4px}
    .chem-badge{padding:7px 9px;border-radius:999px;border:1px solid var(--border);font-size:.75rem;font-weight:900;color:var(--muted)}
    .chem-badge.warn{color:var(--warning);border-color:rgba(251,191,36,.45);background:rgba(251,191,36,.08)}
    .chem-badge.severe{color:var(--danger);border-color:rgba(248,113,113,.5);background:rgba(248,113,113,.10)}

    .safety-table{display:grid;grid-template-columns:1.25fr repeat(3,1fr);gap:10px;align-items:end;margin-top:12px}
    .safety-head{color:var(--muted);font-size:.72rem;text-transform:uppercase;letter-spacing:.10em;font-weight:900}
    .safety-pump{font-weight:900;color:#f8fafc;padding:12px 0}
    .safety-used{font-size:.72rem;color:var(--muted);margin-top:4px}
    @media (max-width: 760px){.safety-table{grid-template-columns:1fr}.safety-head{display:none}.safety-pump{padding-top:8px}}
    .chem-level-bar{margin:12px 0 10px;border-radius:999px;height:18px;overflow:hidden;background:rgba(15,23,42,.82);border:1px solid rgba(148,163,184,.24)}
    .chem-level-fill{height:100%;border-radius:999px;background:linear-gradient(90deg,var(--accent),var(--success));width:0%;transition:width .35s ease}
    .chem-level-fill.warn{background:linear-gradient(90deg,var(--warning),#f97316)}
    .chem-level-fill.severe{background:linear-gradient(90deg,var(--danger),#dc2626)}
    .chem-level-meta{display:flex;justify-content:space-between;gap:10px;align-items:center;font-size:.78rem;color:var(--muted);font-weight:800;margin-bottom:10px}
    .chem-level-percent{font-size:1.1rem;color:#f8fafc;font-weight:950}

    .history-toolbar{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-bottom:14px}
    .history-toolbar button{width:auto;min-width:130px}
    .history-select{width:auto;min-width:150px}
    .history-summary{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin:12px 0 16px}
    .history-stat{border:1px solid rgba(148,163,184,.16);background:rgba(2,6,23,.28);border-radius:14px;padding:12px}
    .history-stat .k{color:var(--muted);font-size:.72rem;text-transform:uppercase;letter-spacing:.10em}
    .history-stat .v{font-size:1.18rem;font-weight:900;color:#f8fafc;margin-top:5px}
    .danger-warning{
      border:2px solid rgba(248,113,113,.78);
      background:linear-gradient(180deg, rgba(127,29,29,.72), rgba(69,10,10,.62));
      color:#fee2e2;
      border-radius:16px;
      padding:14px;
      margin:12px 0;
      font-weight:900;
      box-shadow:0 0 28px rgba(248,113,113,.18);
    }
    .danger-warning small{display:block;margin-top:6px;color:#fecaca;font-weight:700;line-height:1.45}
    .recipe-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:14px;margin-top:14px}
    .recipe-card{border:1px solid rgba(148,163,184,.20);background:rgba(2,6,23,.34);border-radius:18px;padding:16px}
    .recipe-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;margin-bottom:12px}
    .recipe-title{font-size:1.02rem;font-weight:950;color:#f8fafc}
    .recipe-pump{font-size:.72rem;font-weight:900;color:var(--accent);border:1px solid rgba(34,211,238,.32);background:rgba(34,211,238,.08);border-radius:999px;padding:6px 9px;white-space:nowrap}
    .recipe-row{display:grid;grid-template-columns:1fr;gap:8px;margin-top:10px}
    .recipe-label{font-size:.74rem;text-transform:uppercase;letter-spacing:.10em;color:var(--muted);font-weight:850}
    .recipe-note{font-size:.76rem;color:var(--muted);line-height:1.45;margin-top:8px}
    .recipe-result{margin-top:12px;border-radius:14px;padding:12px;background:rgba(34,211,238,.08);border:1px solid rgba(34,211,238,.20)}
    .recipe-result .k{font-size:.72rem;color:var(--muted);text-transform:uppercase;letter-spacing:.10em;font-weight:850}
    .recipe-result .v{font-size:1.12rem;color:var(--accent);font-weight:950;margin-top:4px}
    .recipe-result .u{font-size:.78rem;color:var(--muted);font-weight:800;margin-left:4px}
    .recipe-red-note{
      margin-top:10px;
      padding:10px 12px;
      border-radius:12px;
      border:1px solid rgba(248,113,113,.42);
      background:rgba(127,29,29,.30);
      color:#fecaca;
      font-size:.78rem;
      font-weight:850;
      line-height:1.45;
    }
    .recipe-red-note b{color:#fee2e2}
    .recipe-source-pill{
      display:inline-flex;
      margin-top:8px;
      padding:7px 10px;
      border-radius:999px;
      border:1px solid rgba(251,191,36,.44);
      background:rgba(251,191,36,.10);
      color:#fde68a;
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
    <input type="number" id="tankGal" step="0.1" placeholder="e.g. 120" style="width:100px" oninput="updateRecipePreview()">
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
        <div class="footer-note">This is your local operating mode. It is separate from the 1–7 dosing implementation below.</div>
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
          <option value="7">Mode 7: Kalk + CaCl2 + NaOH + Alk (P1–P4)</option>
        </select>
        <button onclick="saveDosingMode()">Save Dosing Implementation</button>
        <div class="help" id="doseModeHelp">Pump mapping will update immediately below. Mode 7 advanced users can leave NaOH recipe concentration at 144 g/gal unless they intentionally mix a different NaOH solution.</div>
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
          <h3>Active Dosing Plan</h3>
          <span class="meta" id="planRealtimeMeta">Realtime from ESP32</span>
        </div>
        <div id="activePlanList" class="plan-list"></div>
        <div class="footer-note">This shows the current AI plan from <code>/api/status</code>. History graphs still use midnight daily records to keep Firebase/write cost down.</div>
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
  <div class="footer-note">Threshold: bucket amount required before a pump fires. Max Single Dose: cap for one pump run before the 60-second hardware cap. Max Daily Dose: calendar-day limit for each physical pump.</div>
</div>

<div class="card" style="grid-column:1 / -1">
  <div class="card-title">
    <h3>AI Chemistry Safeties</h3>
    <span class="meta">AI plan limits before pump rails</span>
  </div>
  <div class="help">These limits control what the AI is allowed to request chemically. Pump Dosing Safeties still cap what each physical pump can actually run.</div>

  <div class="safety-table">
    <div class="safety-head">Safety</div>
    <div class="safety-head">Value</div>
    <div class="safety-head">Units</div>
    <div class="safety-head">What it protects</div>

    <div class="safety-pump">Max Kalk Per Day</div>
    <input type="number" id="aiSafeMaxKalk" step="1" min="1" onfocus="markAiChemSafetyDirty()" oninput="markAiChemSafetyDirty()">
    <div class="help">mL/day</div>
    <div class="help">Caps AI-requested kalk volume.</div>

    <div class="safety-pump">Max NaOH Per Day</div>
    <input type="number" id="aiSafeMaxNaoh" step="1" min="1" onfocus="markAiChemSafetyDirty()" oninput="markAiChemSafetyDirty()">
    <div class="help">mL/day</div>
    <div class="help">Caps AI-requested high-pH NaOH.</div>

    <div class="safety-pump">Max Alk Solution Per Day</div>
    <input type="number" id="aiSafeMaxAlk" step="1" min="1" onfocus="markAiChemSafetyDirty()" oninput="markAiChemSafetyDirty()">
    <div class="help">mL/day</div>
    <div class="help">Caps AI-requested P4 Alk solution.</div>

    <div class="safety-pump">Max Alk Rise Per Day</div>
    <input type="number" id="aiSafeMaxAlkRise" step="0.05" min="0.05" max="5" onfocus="markAiChemSafetyDirty()" oninput="markAiChemSafetyDirty()">
    <div class="help">dKH/day</div>
    <div class="help">Limits correction aggressiveness before baseline demand is added.</div>

    <div class="safety-pump">Max Mg Correction Per Day</div>
    <input type="number" id="aiSafeMaxMgCorrection" step="1" min="0" onfocus="markAiChemSafetyDirty()" oninput="markAiChemSafetyDirty()">
    <div class="help">mL/day</div>
    <div class="help">Caps one-day Mg correction amount.</div>

    <div class="safety-pump">Max Mg Per Day</div>
    <input type="number" id="aiSafeMaxMg" step="1" min="1" onfocus="markAiChemSafetyDirty()" oninput="markAiChemSafetyDirty()">
    <div class="help">mL/day</div>
    <div class="help">Absolute Mg daily cap after baseline/correction.</div>

    <div class="safety-pump">Mg Deadband</div>
    <input type="number" id="aiSafeMgDeadband" step="1" min="0" max="200" onfocus="markAiChemSafetyDirty()" oninput="markAiChemSafetyDirty()">
    <div class="help">ppm</div>
    <div class="help">Ignores small Mg test noise inside this gap.</div>
  </div>

  <button class="sec" onclick="saveAiChemistrySafeties()" style="margin-top:14px">Update AI Chemistry Safeties</button>
  <div class="footer-note">These are chemistry-planning caps. Per-pump rails below/above still protect the actual pump runtime and daily delivered mL.</div>
</div>

<div class="card" style="grid-column:1 / -1">
  <div class="card-title">
    <h3>Mode 7 Day/Night Alk Split</h3>
    <span class="meta" id="mode7SplitState">Uses light state</span>
  </div>
  <div class="help">Mode 7 only. Controls how alkalinity correction is split between P3 NaOH and P4 Alk based on lights. Existing pH safety still blocks NaOH if pH is too high.</div>
  <div class="two" style="margin-top:12px;">
    <div>
      <div class="help" style="margin-bottom:8px;">Enable Day/Night Split</div>
      <select id="m7SplitEnabled" onfocus="markMode7SplitDirty()" onchange="markMode7SplitDirty()">
        <option value="1">Enabled</option>
        <option value="0">Disabled</option>
      </select>
    </div>
    <div>
      <div class="help" style="margin-bottom:8px;">NaOH pH Cutoff</div>
      <input type="number" id="m7NaohMaxPh" step="0.01" min="7.80" max="8.80" onfocus="markMode7SplitDirty()" oninput="markMode7SplitDirty()">
    </div>
  </div>
  <div class="safety-table" style="margin-top:14px;">
    <div class="safety-head">Light State</div>
    <div class="safety-head">P3 NaOH %</div>
    <div class="safety-head">P4 Alk %</div>
    <div class="safety-head">Meaning</div>

    <div class="safety-pump"><span>Lights ON / Day</span><div class="safety-used">Eric test: Alk only</div></div>
    <input type="number" id="m7DayNaohPct" step="1" min="0" max="100" onfocus="markMode7SplitDirty()" oninput="markMode7SplitDirty()">
    <input type="number" id="m7DayAlkPct" step="1" min="0" max="100" onfocus="markMode7SplitDirty()" oninput="markMode7SplitDirty()">
    <div class="help">Daytime correction favors P4 Alk to avoid pushing pH higher.</div>

    <div class="safety-pump"><span>Lights OFF / Night</span><div class="safety-used">Eric test: NaOH only</div></div>
    <input type="number" id="m7NightNaohPct" step="1" min="0" max="100" onfocus="markMode7SplitDirty()" oninput="markMode7SplitDirty()">
    <input type="number" id="m7NightAlkPct" step="1" min="0" max="100" onfocus="markMode7SplitDirty()" oninput="markMode7SplitDirty()">
    <div class="help">Night correction favors P3 NaOH to support pH while correcting Alk.</div>
  </div>
  <button class="sec" onclick="saveMode7DayNightSplit()" style="margin-top:14px">Save Mode 7 Day/Night Split</button>
  <div class="footer-note">Default test setup: Day = 0% NaOH / 100% Alk. Night = 100% NaOH / 0% Alk. If pH is at or above the cutoff, NaOH is moved/blocked for safety.</div>
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
      <div class="help" style="margin-bottom:8px;">Mg Baseline / Mode 7 Alk Baseline (mL/day)</div>
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
    <h3>Chemical Recipes → Strengths</h3>
    <span class="meta">Easy customer setup</span>
  </div>
  <div class="danger-warning">
    ⚠️ BIG WARNING: These recipes calculate the hidden AI strength numbers.
    <small>These defaults are AI Doser Standard recipes unless the card says otherwise. They are not labeled as BRS unless a verified BRS preset is selected. AIDoser calculates the internal strength from the recipe and tank volume. Lower calculated strength = AI doses more. Higher calculated strength = AI doses less.</small>
  </div>

  <div class="two" style="margin-top:12px;margin-bottom:4px">
    <div>
      <div class="help" style="margin-bottom:8px;">Show recipe notes as</div>
      <select id="recipeUnitMode" onchange="onRecipeUnitChange()">
        <option value="gallon">Per gallon</option>
        <option value="liter">Per liter</option>
      </select>
    </div>
    <div>
      <div class="help" style="margin-bottom:8px;">Recipe note</div>
      <div class="recipe-red-note"><b>Tip:</b> Changing this dropdown only changes the display units on this page. Nothing is saved to the ESP32 until you click Save Recipes + Calculated Strengths.</div>
    </div>
  </div>

  <div class="recipe-grid">
    <div class="recipe-card">
      <div class="recipe-head">
        <div>
          <div class="recipe-title">Kalkwasser</div>
          <div class="recipe-note">Calcium hydroxide solution</div><div class="recipe-source-pill">Recipe Source: AI Doser Standard</div>
        </div>
        <div class="recipe-pump">Kalk</div>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Recipe</div>
        <input type="number" id="recipeKalkGpg" step="0.1" min="0" placeholder="12" onfocus="markChemicalStrengthDirty()" oninput="markChemicalStrengthDirty();updateRecipePreview()">
        <div class="recipe-red-note" id="noteKalk"><b>Recommended:</b> Saturated kalk = 2 tsp, 12 g per gallon of RO/DI water.</div>
      </div>
      <div class="recipe-result"><div class="k">Calculated Strength</div><div class="v"><span id="calcKalk">--</span><span class="u">dKH/mL</span></div></div>
    </div>

    <div class="recipe-card">
      <div class="recipe-head">
        <div>
          <div class="recipe-title">All-For-Reef</div>
          <div class="recipe-note">AFR powder or commercial liquid</div><div class="recipe-source-pill">Recipe Source: Tropic Marin / Custom</div>
        </div>
        <div class="recipe-pump">AFR</div>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Recipe Type</div>
        <select id="recipeAfrType" onchange="markChemicalStrengthDirty();updateRecipePreview()">
          <option value="tm_afr_powder">Tropic Marin AFR Powder Standard</option>
          <option value="custom">Custom AFR / Commercial Liquid</option>
        </select>
        <div class="recipe-red-note" id="noteAfr"><b>Recommended:</b> Tropic Marin AFR powder = 160 g/L or 606 g/gal final solution.</div>
      </div>
      <div class="recipe-row" id="afrCustomBox" style="display:none">
        <div class="recipe-label">Custom AFR Strength</div>
        <input type="number" id="strAfr" step="0.0000001" min="0.0000001" onfocus="markChemicalStrengthDirty()" oninput="markChemicalStrengthDirty();updateRecipePreview()">
        <div class="recipe-note">Only for premixed AFR or another commercial liquid.</div>
      </div>
      <div class="recipe-result"><div class="k">Calculated Strength</div><div class="v"><span id="calcAfr">--</span><span class="u">dKH/mL</span></div></div>
    </div>

    <div class="recipe-card">
      <div class="recipe-head">
        <div>
          <div class="recipe-title">Alkalinity</div>
          <div class="recipe-note">Soda ash or baking soda</div>
        </div>
        <div class="recipe-pump">Alk</div>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Recipe Source</div>
        <select id="recipeAlkSource" onchange="applyRecipeSourceDefaults()">
          <option value="aid_standard">AI Doser Standard</option>
          <option value="brs_not_set">BRS Recipe - verify before use</option>
          <option value="custom">Custom</option>
        </select>
        <div class="recipe-note">Current default is AI Doser Standard, not BRS.</div>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Chemical Type</div>
        <select id="recipeAlkType" onchange="markChemicalStrengthDirty();updateRecipePreview()">
          <option value="soda_ash">Soda Ash / Sodium Carbonate</option>
          <option value="baking_soda">Baking Soda / Sodium Bicarbonate</option>
        </select>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Recipe: <span class="recipeUnitLabel">grams per gallon</span></div>
        <input type="number" id="recipeAlkGpg" step="0.1" min="0" placeholder="100" onfocus="markChemicalStrengthDirty()" oninput="markChemicalStrengthDirty();updateRecipePreview()">
      </div>
      <div class="recipe-red-note" id="noteAlk"><b>Recommended:</b> Soda Ash = 100 g/gal or 26.4 g/L RO/DI water.</div>
      <div class="recipe-result"><div class="k">Calculated Strength</div><div class="v"><span id="calcAlk">--</span><span class="u">dKH/mL</span></div></div>
    </div>

    <div class="recipe-card">
      <div class="recipe-head">
        <div>
          <div class="recipe-title">Sodium Hydroxide (NaOH)</div>
          <div class="recipe-note">Advanced high-pH alkalinity • mainly for Mode 6 / Mode 7</div>
        </div>
        <div class="recipe-pump">NaOH</div>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Recipe Source</div>
        <select id="recipeNaohSource" onchange="applyRecipeSourceDefaults()">
          <option value="aid_standard">AI Doser Standard for Mode 6 / Mode 7</option>
          <option value="custom">Custom NaOH Recipe</option>
        </select>
        <div class="recipe-note">Recommended for Mode 7: leave this at AI Doser Standard unless intentionally mixed differently.</div>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Recipe Concentration: <span class="recipeUnitLabel">grams per gallon</span></div>
        <input type="number" id="recipeNaohGpg" step="0.1" min="0" placeholder="144" onfocus="markChemicalStrengthDirty()" oninput="markChemicalStrengthDirty();updateRecipePreview()">
        <div class="recipe-red-note" id="noteNaoh"><b>AI Doser Standard:</b> 144 g/gal or 38.0 g/L NaOH in RO/DI water. Do not change unless you intentionally mix a different concentration.</div>
      </div>
      <div class="recipe-result"><div class="k">Calculated Strength Saved to AI</div><div class="v"><span id="calcNaoh">--</span><span class="u">dKH/mL</span></div></div>
    </div>

    <div class="recipe-card">
      <div class="recipe-head">
        <div>
          <div class="recipe-title">Calcium Chloride</div>
          <div class="recipe-note">Calcium supplement</div>
        </div>
        <div class="recipe-pump">CaCl2</div>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Recipe Source</div>
        <select id="recipeCacl2Source" onchange="applyRecipeSourceDefaults()">
          <option value="aid_standard">AI Doser Standard</option>
          <option value="brs_not_set">BRS Recipe - verify before use</option>
          <option value="custom">Custom</option>
        </select>
        <div class="recipe-note">Current default is AI Doser Standard, not BRS.</div>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Chemical Type</div>
        <select id="recipeCacl2Type" onchange="markChemicalStrengthDirty();updateRecipePreview()">
          <option value="cacl2_dihydrate">Calcium Chloride Dihydrate</option>
          <option value="cacl2_anhydrous">Calcium Chloride Anhydrous</option>
        </select>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Recipe: <span class="recipeUnitLabel">grams per gallon</span></div>
        <input type="number" id="recipeCacl2Gpg" step="0.1" min="0" placeholder="250" onfocus="markChemicalStrengthDirty()" oninput="markChemicalStrengthDirty();updateRecipePreview()">
      </div>
      <div class="recipe-red-note" id="noteCacl2"><b>Recommended:</b> Calcium Chloride Dihydrate = 250 g/gal or 66.0 g/L RO/DI water.</div>
      <div class="recipe-result"><div class="k">Calculated Strength</div><div class="v"><span id="calcCacl2">--</span><span class="u">ppm/mL</span></div></div>
    </div>

    <div class="recipe-card">
      <div class="recipe-head">
        <div>
          <div class="recipe-title">Magnesium</div>
          <div class="recipe-note">Mag chloride or Epsom salt</div>
        </div>
        <div class="recipe-pump">Mg</div>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Recipe Source</div>
        <select id="recipeMgSource" onchange="applyRecipeSourceDefaults()">
          <option value="aid_standard">AI Doser Standard</option>
          <option value="brs_not_set">BRS Recipe - verify before use</option>
          <option value="custom">Custom</option>
        </select>
        <div class="recipe-note">Current default is AI Doser Standard, not BRS.</div>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Chemical Type</div>
        <select id="recipeMgType" onchange="markChemicalStrengthDirty();updateRecipePreview()">
          <option value="mag_chloride">Magnesium Chloride Hexahydrate</option>
          <option value="epsom">Epsom Salt / Magnesium Sulfate</option>
        </select>
      </div>
      <div class="recipe-row">
        <div class="recipe-label">Recipe: <span class="recipeUnitLabel">grams per gallon</span></div>
        <input type="number" id="recipeMgGpg" step="0.1" min="0" placeholder="500" onfocus="markChemicalStrengthDirty()" oninput="markChemicalStrengthDirty();updateRecipePreview()">
      </div>
      <div class="recipe-red-note" id="noteMg"><b>Recommended:</b> Magnesium Chloride Hexahydrate = 500 g/gal or 132.1 g/L RO/DI water.</div>
      <div class="recipe-result"><div class="k">Calculated Strength</div><div class="v"><span id="calcMg">--</span><span class="u">ppm/mL</span></div></div>
    </div>

    <div class="recipe-card recipe-full">
      <div class="recipe-head">
        <div>
          <div class="recipe-title">Summary</div>
          <div class="recipe-note">These are the internal values saved to the ESP32. Customers usually only need the recipe cards above.</div>
        </div>
        <div class="recipe-pump" id="recipeTankSummary">Using tank volume</div>
      </div>
      <button class="sec" style="margin-bottom:12px" onclick="updateRecipePreview()">Recalculate Chemical Strengths</button>
      <div class="help" id="recipePreview">Enter tank volume and recipes to calculate.</div>

      <details style="margin-top:14px">
        <summary class="help" style="cursor:pointer;color:var(--accent);font-weight:900">Advanced: raw internal strength fields</summary>
        <div class="two" style="margin-top:12px">
          <input type="number" id="strKalk" step="0.0000001" min="0.0000001" onfocus="markChemicalStrengthDirty()" oninput="markChemicalStrengthDirty()">
          <input type="number" id="strAlk" step="0.0000001" min="0.0000001" onfocus="markChemicalStrengthDirty()" oninput="markChemicalStrengthDirty()">
          <input type="number" id="strNaoh" step="0.0000001" min="0.0000001" onfocus="markChemicalStrengthDirty()" oninput="markChemicalStrengthDirty()">
          <input type="number" id="strMg" step="0.00001" min="0.00001" onfocus="markChemicalStrengthDirty()" oninput="markChemicalStrengthDirty()">
          <input type="number" id="strCacl2" step="0.00001" min="0.00001" onfocus="markChemicalStrengthDirty()" oninput="markChemicalStrengthDirty()">
        </div>
      </details>
    </div>
  </div>

  <button class="danger" onclick="saveChemicalStrengths()" style="margin-top:14px">Save Recipes + Calculated Strengths</button>
  <div class="footer-note">Recipes are customer-friendly. Internal strengths are calculated automatically from grams/gallon and tank volume.</div>
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
    },
    7: {
      title: "Mode 7 • Kalk + CaCl2 + NaOH + Alk",
      pumps: [
        {index:0,key:"kalk",name:"Kalk (Pump 1)"},
        {index:1,key:"cacl2",name:"CaCl2 (Pump 2)"},
        {index:2,key:"naoh",name:"NaOH (Pump 3)"},
        {index:3,key:"alk",name:"Alk (Pump 4 / old Mg pump)"}
      ]
    }
  };

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

  function getModeCfg(mode){
    return DOSING_MODES[Number(mode)] || DOSING_MODES[1];
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


  // LOCAL DASHBOARD PLAN SOURCE
  // This page is served by the ESP32, so the Active Dosing Plan must come from
  // /api/status only.  Firebase is not used for this card.
  function planFromStatus(s){
    if (!s) return {};
    return s.dosingMlPerDay || s.aiPlan || s.plan || s.currentPlan || {};
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
    document.getElementById('onlineDot').style.boxShadow = s.wifiConnected ? '0 0 10px rgba(74,222,128,.8)' : '0 0 10px rgba(248,113,113,.8)';
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

  async function saveDosingMode(){
    const dosingMode = parseInt(document.getElementById('dosingModeSelect').value, 10);
    await api('/api/dosing-mode', 'POST', { dosingMode });
    await loadAll();
    alert('Dosing implementation saved.');
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

async function saveChemicalStrengths(){
  const calc = calcRecipeStrengths();
  if (!calc) {
    alert('Enter a valid tank volume before saving chemical recipes.');
    return;
  }

  const s = calc.strengths;
  if (!Number.isFinite(s.kalk) || s.kalk <= 0 ||
      !Number.isFinite(s.alk) || s.alk <= 0 ||
      !Number.isFinite(s.naoh) || s.naoh <= 0 ||
      !Number.isFinite(s.mg) || s.mg <= 0 ||
      !Number.isFinite(s.cacl2) || s.cacl2 <= 0) {
    alert('Enter valid recipe gram-per-gallon values greater than zero.');
    return;
  }

  const payload = {
    kalk: s.kalk,
    afr: s.afr,
    alk: s.alk,
    naoh: s.naoh,
    mg: s.mg,
    cacl2: s.cacl2,
    recipe: calc.recipe
  };

  const res = await api('/api/config/chemical-strengths', 'POST', payload);
  if (res && res.ok === false) {
    alert('Chemical recipe save failed: ' + (res.error || res.raw || 'unknown error'));
    return;
  }

  currentStatus.chemicalStrengths = payload;
  currentStatus.chemicalRecipes = calc.recipe;
  clearChemicalStrengthDirty();
  await loadAll();
  alert('Chemical recipes and calculated strengths saved.');
}



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

async function saveAiChemistrySafeties(){
  let payload;
  try {
    payload = {
      maxKalkDayMl: readPositiveNumber('aiSafeMaxKalk', 'Max Kalk Per Day'),
      maxNaohDayMl: readPositiveNumber('aiSafeMaxNaoh', 'Max NaOH Per Day'),
      maxAlkDayMl: readPositiveNumber('aiSafeMaxAlk', 'Max Alk Solution Per Day'),
      maxAlkRiseDkhDay: readPositiveNumber('aiSafeMaxAlkRise', 'Max Alk Rise Per Day'),
      maxMgCorrectionDayMl: readPositiveNumber('aiSafeMaxMgCorrection', 'Max Mg Correction Per Day', true),
      maxMgDayMl: readPositiveNumber('aiSafeMaxMg', 'Max Mg Per Day'),
      mgDeadbandPpm: readPositiveNumber('aiSafeMgDeadband', 'Mg Deadband', true)
    };
  } catch (err) {
    alert(err.message || err);
    return;
  }

  const res = await api('/api/config/ai-chemistry-safeties', 'POST', payload);
  if (res && res.ok === false) {
    alert('AI chemistry safety save failed: ' + (res.error || res.raw || 'unknown error'));
    return;
  }

  currentStatus.aiChemistrySafeties = payload;
  clearAiChemSafetyDirty();
  await loadAll();
  alert('AI Chemistry Safeties Updated');
}

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

async function saveMode7DayNightSplit(){
  let payload;
  try {
    payload = {
      enabled: gv('m7SplitEnabled') !== '0',
      dayNaohPct: readPctInput('m7DayNaohPct', 0),
      dayAlkPct: readPctInput('m7DayAlkPct', 100),
      nightNaohPct: readPctInput('m7NightNaohPct', 100),
      nightAlkPct: readPctInput('m7NightAlkPct', 0),
      naohMaxPh: Number(gv('m7NaohMaxPh'))
    };
    if (!Number.isFinite(payload.naohMaxPh) || payload.naohMaxPh < 7.80 || payload.naohMaxPh > 8.80) {
      throw new Error('Enter a valid NaOH pH cutoff from 7.80 to 8.80.');
    }
    if ((payload.dayNaohPct + payload.dayAlkPct) <= 0 || (payload.nightNaohPct + payload.nightAlkPct) <= 0) {
      throw new Error('Day and night split totals must be greater than zero.');
    }
  } catch (err) {
    alert(err.message || err);
    return;
  }

  const res = await api('/api/config/mode7-split', 'POST', payload);
  if (res && res.ok === false) {
    alert('Mode 7 split save failed: ' + (res.error || res.raw || 'unknown error'));
    return;
  }

  currentStatus.mode7DayNightSplit = payload;
  clearMode7SplitDirty();
  await loadAll();
  alert('Mode 7 Day/Night Alk Split saved.');
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
      plugins: { legend: { labels: { color: '#e2e8f0' } } },
      scales: {
        x: { grid: { display: false }, ticks: { color: '#94a3b8', maxTicksLimit: 8 } },
        y: { position: 'left', grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#94a3b8' } },
        y1: { position: 'right', grid: { display: false }, ticks: { color: '#94a3b8' }, min: 7.7, max: 8.7, title: { display: true, text: 'pH', color: '#94a3b8' } }
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
      plugins: { legend: { labels: { color: '#e2e8f0' } } },
      scales: {
        x: { grid: { display: false }, ticks: { color: '#94a3b8', maxTicksLimit: 8 } },
        y: { grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#94a3b8' }, beginAtZero: true }
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
      plugins: { legend: { labels: { color: '#e2e8f0' } } },
      scales: {
        x: { grid: { display: false }, ticks: { color: '#94a3b8', maxTicksLimit: 8 } },
        y: { grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#94a3b8' }, beginAtZero: true }
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
        legend: { position: 'top', labels: { color: '#e2e8f0', usePointStyle: true } },
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
          ticks: { color: '#94a3b8', maxRotation: 0, autoSkip: true, maxTicksLimit: 12 },
          title: { display: true, text: 'Day', color: '#94a3b8' }
        },
        y: {
          stacked: true,
          beginAtZero: true,
          grid: { color: 'rgba(255,255,255,0.06)' },
          ticks: { color: '#94a3b8' },
          title: { display: true, text: 'Actual dosed mL/day', color: '#94a3b8' }
        },
        yTotal: {
          position: 'right',
          beginAtZero: true,
          grid: { display: false },
          ticks: { color: '#94a3b8' },
          title: { display: true, text: 'Total mL/day', color: '#94a3b8' }
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
    { type: 'bar', label: pumpLabelForHistory(mode, 0), data: rows.map(r => r.p1), stack: 'actual', backgroundColor: 'rgba(34,211,238,0.75)', borderColor: '#22d3ee', borderWidth: 1 },
    { type: 'bar', label: pumpLabelForHistory(mode, 1), data: rows.map(r => r.p2), stack: 'actual', backgroundColor: 'rgba(74,222,128,0.72)', borderColor: '#4ade80', borderWidth: 1 },
    { type: 'bar', label: pumpLabelForHistory(mode, 2), data: rows.map(r => r.p3), stack: 'actual', backgroundColor: 'rgba(251,191,36,0.72)', borderColor: '#fbbf24', borderWidth: 1 },
    { type: 'bar', label: pumpLabelForHistory(mode, 3), data: rows.map(r => r.p4), stack: 'actual', backgroundColor: 'rgba(248,113,113,0.72)', borderColor: '#f87171', borderWidth: 1 },
    { type: 'line', label: 'Total actual', data: rows.map(r => r.total), yAxisID: 'yTotal', tension: 0.25, borderColor: '#f8fafc', backgroundColor: '#f8fafc', pointRadius: 3, pointHoverRadius: 5, borderWidth: 2 }
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
