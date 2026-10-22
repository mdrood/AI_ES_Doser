// =============================================================================
// AIDoser TEST-ONLY: "Reset & Start Over" bar.
// Works on the cloud pages (device-setup.html, index.html). One click:
//   1. the AIDoser wipes its own wizard/setup data (via Firebase command),
//   2. the claim is released and this login is deleted (Cloud Function),
//   3. you land back on /setup.html as a brand-new user.
// Needs: firebase-app, firebase-auth and firebase-functions compat scripts
// already on the page, and /test-config.js loaded before this file.
// Does nothing unless window.AIDOSER_TEST_BUILD === true.
// =============================================================================
(function () {
  if (window.AIDOSER_TEST_BUILD !== true) return;

  var REMEMBER_KEY = "aidoserTestDeviceId";

  function currentDeviceId() {
    var fromUrl = new URLSearchParams(window.location.search).get("device") || "";
    try {
      if (fromUrl) { localStorage.setItem(REMEMBER_KEY, fromUrl); return fromUrl; }
      // index.html remembers the active device here:
      return localStorage.getItem("activeDeviceId") || localStorage.getItem(REMEMBER_KEY) || "";
    } catch (e) { return fromUrl; }
  }

  var bar = document.createElement("div");
  bar.id = "aidoserTestBar";
  bar.style.cssText =
    "position:fixed;top:0;left:0;right:0;z-index:2000;height:44px;display:none;" +
    "align-items:center;justify-content:center;gap:14px;padding:0 12px;" +
    "background:#7a1020;color:#fff;font:700 13px Arial,Helvetica,sans-serif;" +
    "border-bottom:2px solid #ff6b6b;";

  var label = document.createElement("span");
  var btn = document.createElement("button");
  btn.type = "button";
  btn.textContent = "Reset & Start Over";
  btn.style.cssText =
    "border:0;border-radius:8px;padding:7px 14px;font:800 13px Arial,Helvetica,sans-serif;" +
    "cursor:pointer;color:#7a1020;background:#fff;";
  var status = document.createElement("span");
  status.style.cssText = "font-weight:400;opacity:.95;";

  bar.appendChild(label);
  bar.appendChild(btn);
  bar.appendChild(status);

  function refreshLabel() {
    var id = currentDeviceId();
    label.textContent = "TEST BUILD" + (id ? " • " + id : "");
  }

  function mount() {
    document.body.appendChild(bar);
    refreshLabel();
    // Only show the bar while someone is signed in (reset needs the login).
    try {
      firebase.auth().onAuthStateChanged(function (user) {
        var show = !!user;
        bar.style.display = show ? "flex" : "none";
        document.body.style.paddingTop = show ? "44px" : "";
        refreshLabel();
      });
    } catch (e) {
      bar.style.display = "flex";
      document.body.style.paddingTop = "44px";
    }
  }
  if (document.body) mount();
  else document.addEventListener("DOMContentLoaded", mount);

  function clearLocalState() {
    try {
      Object.keys(sessionStorage).forEach(function (k) {
        if (k.indexOf("aidoserWizardStep_") === 0) sessionStorage.removeItem(k);
      });
      localStorage.removeItem("activeDeviceId");
    } catch (e) {}
  }

  btn.addEventListener("click", async function () {
    var deviceId = currentDeviceId();
    if (!deviceId) {
      status.textContent = "No device ID known. Open the page with ?device=reefDoserN once.";
      return;
    }
    var ok = window.confirm(
      "TEST RESET for " + deviceId + "\n\nThis will:\n" +
      " • wipe the AIDoser's wizard setup (tank, chemicals, calibration, targets)\n" +
      " • release the claim code so it can be used again\n" +
      " • DELETE this login account\n\n" +
      "The AIDoser must be powered on and online. Continue?"
    );
    if (!ok) return;

    btn.disabled = true;
    status.textContent = "Resetting... waiting for the AIDoser to confirm (up to 25 s)";
    try {
      var fn = firebase.app().functions("us-central1").httpsCallable("testResetDevice");
      await fn({ deviceId: deviceId });
      status.textContent = "Done. Returning to setup...";
    } catch (err) {
      console.error(err);
      status.textContent = "Reset failed: " + (err && err.message ? err.message : err);
      btn.disabled = false;
      return;
    }

    clearLocalState();
    try { await firebase.auth().signOut(); } catch (e) { /* account is already gone */ }
    window.location.href = "/setup.html";
  });
})();
