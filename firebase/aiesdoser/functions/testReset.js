// =============================================================================
// AIDoser TEST-ONLY Cloud Function: testResetDevice
//
// Puts a device back to its "just manufactured" state so the claim code and
// setup wizard can be walked through again as a brand-new user.
//
// What is KEPT (written by scripts/manufacture_device.py, restored verbatim):
//   /deviceClaims/{deviceId}  claimCode, deviceId, label, macAddress, chipId,
//                             board, batch, sequence, assignedAt, enabled
//   /claimInventory/{code}    status "ASSIGNED", batch, sequence, deviceId,
//                             label, macAddress, chipId, assignedAt
//
// What is REMOVED:
//   - everything else claimDevice added to those two nodes
//     (claimed/claimedBy/claimedAt/ownerUid, terms acceptance fields, ...)
//     because each node is re-written with set(), not merged
//   - devices/{id}/settings (tankSize, doseSchedule, dosingMode), calibration
//   - users/{uid}/devices/{id} (the link the dashboard uses to find the device)
//   - the calling user's Auth account + users/{uid}, unless it is the admin
//     login or it also owns other devices (then only the device is un-linked)
//
// DEVICE SIDE: before touching anything in the cloud, this writes
//   devices/{id}/commands/testReset = { trigger:true, nonce, ts, requestedBy }
// and waits (up to 25 s) for the controller to write back
//   devices/{id}/commands/testReset/ack = <same nonce string>
// after it has wiped its own wizard/setup data (see firmware/lib/WebRoutes/TestReset.cpp).
// No ack -> nothing is changed and the tester sees "device did not confirm".
// Set AIDOSER_SKIP_DEVICE_RESET=true in functions/.env to skip the device step
// (useful until the firmware part is flashed).
//
// Server-side kill switch: runs ONLY when AIDOSER_TEST_BUILD=true in
// functions/.env. Production: leave it unset/false (or don't export this).
//
// In functions/index.js add:
//   exports.testResetDevice = require("./testReset").testResetDevice;
// =============================================================================
const { onCall, HttpsError } = require("firebase-functions/v2/https");
const admin = require("firebase-admin");

const TEST_BUILD = process.env.AIDOSER_TEST_BUILD === "true";
const SKIP_DEVICE_RESET = process.env.AIDOSER_SKIP_DEVICE_RESET === "true";

// Cloud Functions are shared with real customers. Only device IDs listed here
// can ever be reset, so a customer calling this function directly cannot touch
// their own device. Set in functions/.env, comma separated, e.g.
//   AIDOSER_TEST_DEVICE_IDS=reefDoser4,reefDoser5
const TEST_DEVICE_IDS = String(process.env.AIDOSER_TEST_DEVICE_IDS || "")
  .split(",").map((x) => x.trim()).filter(Boolean);
const DEVICE_ACK_TIMEOUT_MS = 25000;

// Resolves true when `ref` becomes equal to `expected`, false on timeout.
function waitForAck(ref, expected, timeoutMs) {
  return new Promise((resolve) => {
    let finished = false;
    let timer = null;
    const handler = (snap) => { if (snap.val() === expected) finish(true); };
    function finish(result) {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      ref.off("value", handler);
      resolve(result);
    }
    timer = setTimeout(() => finish(false), timeoutMs);
    ref.on("value", handler);
  });
}

// Never delete this login (it is the admin account that owns the admin tools
// on the dashboard). Same address as MARK_ADMIN_EMAIL in public/index.html.
const PROTECTED_EMAILS = ["markdamonrood@gmail.com"];

// Cloud-side setup data the dashboard writes for a device. Cleared on reset so
// the next run starts from empty. (device-owned nodes such as state/, sensors/
// and history are left alone.)
const deviceSetupPaths = (deviceId) => [
  `devices/${deviceId}/settings/tankSize`,
  `devices/${deviceId}/settings/doseSchedule`,
  `devices/${deviceId}/settings/dosingMode`,
  `devices/${deviceId}/calibration`,
  `devices/${deviceId}/commands/calibrate`,
];

function pick(obj, keys) {
  const out = {};
  keys.forEach((k) => {
    if (obj && obj[k] !== undefined && obj[k] !== null) out[k] = obj[k];
  });
  return out;
}

// 2nd-gen callable (Node 24 projects cannot create 1st-gen functions).
exports.testResetDevice = onCall(
  { region: "us-central1", timeoutSeconds: 60 },
  async (request) => {
    const data = request.data;
    const context = { auth: request.auth };

    if (!TEST_BUILD) {
      throw new HttpsError("failed-precondition", "Test reset is disabled on this deployment.");
    }
    if (!context.auth) {
      throw new HttpsError("unauthenticated", "Sign in first.");
    }

    const deviceId = String((data && data.deviceId) || "").trim();
    if (!/^reefDoser\d+$/.test(deviceId)) {
      throw new HttpsError("invalid-argument", "Device ID must look like reefDoser4.");
    }

    if (!TEST_DEVICE_IDS.includes(deviceId)) {
      throw new HttpsError("permission-denied", `${deviceId} is not on the test-device list.`);
    }

    const uid = context.auth.uid;
    const db = admin.database();
    const claimRef = db.ref(`/deviceClaims/${deviceId}`);
    const claim = (await claimRef.get()).val();
    if (!claim) throw new HttpsError("not-found", "No claim record for that device.");

    // Only the account that owns the claim may reset it.
    const owner = claim.claimedBy || claim.ownerUid || claim.uid || null;
    if (owner && owner !== uid) {
      throw new HttpsError("permission-denied", "This device is claimed by a different account.");
    }
    if (!claim.claimCode) {
      throw new HttpsError("failed-precondition", "Claim record has no claimCode; cannot restore.");
    }

    const claimCode = String(claim.claimCode);
    const invRef = db.ref(`/claimInventory/${claimCode}`);
    const inv = (await invRef.get()).val() || {};

    // Restore /deviceClaims/{deviceId} to what bind_claim_in_firebase() wrote.
    const restoredClaim = Object.assign(
      pick(claim, ["claimCode", "deviceId", "label", "macAddress", "chipId", "board", "batch", "sequence", "assignedAt"]),
      { claimed: false, enabled: true }
    );
    if (restoredClaim.assignedAt === undefined) restoredClaim.assignedAt = { ".sv": "timestamp" };

    // Restore /claimInventory/{claimCode} to its ASSIGNED state.
    const restoredInv = Object.assign(
      pick(Object.assign({}, claim, inv), ["batch", "sequence", "deviceId", "label", "macAddress", "chipId"]),
      { status: "ASSIGNED", assignedAt: inv.assignedAt !== undefined ? inv.assignedAt : restoredClaim.assignedAt }
    );

    // Step 1: make the controller wipe its own wizard/setup data FIRST, so a
    // device that is offline leaves everything untouched (no half reset).
    if (!SKIP_DEVICE_RESET) {
      const cmdRef = db.ref(`/devices/${deviceId}/commands/testReset`);
      // Text token (not a number): ms timestamps are too big to round-trip
      // reliably through the ESP32 Firebase library, strings are exact.
      const nonce = `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
      await cmdRef.set({ trigger: true, nonce, ts: Date.now(), requestedBy: uid });
      const acked = await waitForAck(cmdRef.child("ack"), nonce, DEVICE_ACK_TIMEOUT_MS);
      await cmdRef.remove();
      if (!acked) {
        throw new HttpsError(
          "deadline-exceeded",
          "The AIDoser did not confirm the reset. Make sure it is powered on and online, then try again. Nothing was changed."
        );
      }
    }

    // Step 2: cloud side.
    await claimRef.set(restoredClaim);
    await invRef.set(restoredInv);

    // Cloud-side setup data for this device (tank size, schedule, mode, calibration).
    for (const path of deviceSetupPaths(deviceId)) {
      await db.ref(path).remove();
    }

    // Unlink the device from this user. The dashboard finds a user's devices at
    // users/{uid}/devices, so removing this entry is what makes them "new".
    const devicesSnap = await db.ref(`/users/${uid}/devices`).get();
    const mapped = Object.keys(devicesSnap.val() || {});
    await db.ref(`/users/${uid}/devices/${deviceId}`).remove();
    const otherDevices = mapped.filter((id) => id !== deviceId);

    // Delete the login last, and only if it is a throwaway test account.
    const email = String((context.auth.token && context.auth.token.email) || "").toLowerCase();
    let accountDeleted = false;
    if (PROTECTED_EMAILS.includes(email)) {
      // Keep the admin login; the device is still un-claimed and unlinked.
    } else if (otherDevices.length > 0) {
      // This login also owns other devices; do not delete it.
    } else {
      await db.ref(`/users/${uid}`).remove();
      await admin.auth().deleteUser(uid);
      accountDeleted = true;
    }

    return { ok: true, deviceId, claimCode, accountDeleted };
  }
);
