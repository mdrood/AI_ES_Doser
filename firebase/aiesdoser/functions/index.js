const admin = require("firebase-admin");
const crypto = require("crypto");
const { onValueWritten } = require("firebase-functions/v2/database");
const { onSchedule } = require("firebase-functions/v2/scheduler");
const { onCall, HttpsError } = require("firebase-functions/v2/https");
const { logger } = require("firebase-functions");

admin.initializeApp();

const DATABASE_INSTANCE = "aiesdoser-default-rtdb";

const CURRENT_TERMS_VERSION = "2026-06-v1";
const CURRENT_TERMS_SHA256 = "b52bf7079294df46c55e062f9324278c680206806e41b2cf609703c7f662e8fd";


function rank(level) {
  if (level === "severe") return 3;
  if (level === "warning") return 2;
  if (level === "info") return 1;
  return 0;
}

function shouldPassUserLevel(alertLevel, setting) {
  if (!setting || setting === "warning") setting = "warning";
  if (setting === "muted") return false;
  return rank(alertLevel) >= rank(setting);
}

function cooldownMs(level) {
  if (level === "severe") return 30 * 60 * 1000;
  if (level === "warning") return 4 * 60 * 60 * 1000;
  if (level === "info") return 24 * 60 * 60 * 1000;
  return 4 * 60 * 60 * 1000;
}

async function getEnabledTokens(deviceId) {
  const snap = await admin.database()
    .ref(`/devices/${deviceId}/fcmTokens`)
    .once("value");

  const tokens = [];

  snap.forEach((child) => {
    const val = child.val();
    if (!val) return;

    if (val.active !== false && val.token) {
      tokens.push(val.token);
    }
  });

  return [...new Set(tokens)];
}

async function sendToDeviceTokens(deviceId, title, body, data = {}) {
  const tokens = await getEnabledTokens(deviceId);

  if (!tokens.length) {
    logger.info(`No FCM tokens registered for ${deviceId}; push skipped.`);
    return false;
  }

  const response = await admin.messaging().sendEachForMulticast({
    tokens,
    notification: { title, body },
    data,
    webpush: {
      fcmOptions: { link: "https://aiesdoser.web.app" },
      notification: {
        icon: "/icons/icon-192.png",
        badge: "/icons/icon-192.png",
      },
    },
  });

  logger.info(
    `FCM sent for ${deviceId}: success=${response.successCount} failure=${response.failureCount}`
  );

  return response.successCount > 0;
}

exports.sendAlertNotification = onValueWritten(
  {
    ref: "/devices/{deviceId}/alertState",
    instance: DATABASE_INSTANCE,
    region: "us-central1",
  },
  async (event) => {
    const before = event.data.before.val();
    const after = event.data.after.val();

    if (!after) return;

    const wasActive = before && before.active === true;
    const isActive = after.active === true;

    const deviceId = event.params.deviceId;
    const level = String(after.level || "info");
    const code = String(after.code || "ALERT");
    const message = String(after.message || "AIDoser alert");

    // =========================
    // RECOVERY NOTIFICATION
    // =========================
    if (wasActive && !isActive) {

      await sendToDeviceTokens(
        deviceId,
        `AIDoser RECOVERY: ${deviceId}`,
        `${code} is back in range / cleared.`,
        {
          deviceId,
          level: "info",
          code: `${code}_RECOVERY`,
          message: `${code} recovered`,
        }
      );

      await admin.database()
        .ref(`/devices/${deviceId}/alertPushState/${code}`)
        .update({
          recoveredAtMs: Date.now(),
          recoveredMessage: `${code} is back in range / cleared.`,
        });

      logger.info(`Recovery notification sent for ${deviceId} ${code}`);

      return;
    }

    // Ignore inactive writes unless recovery.
    if (!isActive) return;

    const settingSnap = await admin.database()
      .ref(`/devices/${deviceId}/settings/notificationLevel`)
      .once("value");

    const notificationLevel = settingSnap.val() || "warning";

    if (!shouldPassUserLevel(level, notificationLevel)) {
      logger.info(`Alert ${code} suppressed by setting ${notificationLevel}`);
      return;
    }

    const pushStateRef = admin.database()
      .ref(`/devices/${deviceId}/alertPushState/${code}`);

    const pushStateSnap = await pushStateRef.once("value");
    const lastPushMs = Number(pushStateSnap.child("lastPushMs").val() || 0);
    const now = Date.now();

    if (now - lastPushMs < cooldownMs(level)) {
      logger.info(`Alert ${code} suppressed by cooldown.`);
      return;
    }

    await sendToDeviceTokens(
      deviceId,
      `AIDoser ${level.toUpperCase()}: ${deviceId}`,
      message,
      {
        deviceId,
        level,
        code,
        message,
      }
    );

    await pushStateRef.update({
      lastPushMs: now,
      level,
      message,
    });
  }
);

exports.checkDeviceOffline = onSchedule(
  {
    schedule: "every 5 minutes",
    region: "us-central1",
  },
  async () => {
    const devicesSnap = await admin.database().ref("/devices").once("value");
    const nowSec = Math.floor(Date.now() / 1000);

    const updates = [];

    devicesSnap.forEach((deviceSnap) => {
      const deviceId = deviceSnap.key;
      const val = deviceSnap.val() || {};

      const lastSeenUnix = Number(
        val.state && val.state.lastSeenUnix ? val.state.lastSeenUnix : 0
      );

      if (!lastSeenUnix) return;

      const ageSec = nowSec - lastSeenUnix;

      const alertState = val.alertState || {};

      const alreadyOffline =
        alertState.active === true &&
        (
          alertState.code === "OFFLINE_15_MIN" ||
          alertState.code === "OFFLINE_30_MIN"
        );

      const alertRef = admin.database().ref(`/devices/${deviceId}/alertState`);

      // Trigger offline only once.
      if (ageSec >= 30 * 60 && !alreadyOffline) {
        updates.push(
          alertRef.update({
            active: true,
            level: "severe",
            code: "OFFLINE_30_MIN",
            message: `Device has been offline/stale for ${Math.round(ageSec / 60)} minutes.`,
            source: "cloud-offline-check",
            deviceId,
            updatedAtCloudMs: Date.now(),
          })
        );
      }

      // Auto-clear offline alert when device comes back.
      if (ageSec < 10 * 60 && alreadyOffline) {
        updates.push(
          alertRef.update({
            active: false,
            level: "normal",
            code: "OK",
            message: "Device is back online.",
            source: "cloud-offline-check",
            deviceId,
            updatedAtCloudMs: Date.now(),
          })
        );
      }
    });

    await Promise.all(updates);
  }
);

function cleanText(value, maxLength) {
  return String(value || "").trim().slice(0, maxLength);
}

function normalizeDeviceId(value) {
  const deviceId = cleanText(value, 40);

  if (!/^reefDoser\d+$/.test(deviceId)) {
    throw new HttpsError("invalid-argument", "Invalid AIDoser device ID.");
  }

  return deviceId;
}

function normalizeClaimCode(value) {
  const claimCode = cleanText(value, 40).toUpperCase();

  if (claimCode.length < 6) {
    throw new HttpsError("invalid-argument", "Invalid claim code.");
  }

  return claimCode;
}

exports.claimDevice = onCall(
  {
    region: "us-central1",
  },
  async (request) => {
    if (!request.auth) {
      throw new HttpsError(
        "unauthenticated",
        "You must create or sign in to an account first."
      );
    }

    const uid = request.auth.uid;
    const email = cleanText(request.auth.token.email, 160);
    const customerName = cleanText(request.data.customerName, 80);
    const alias = cleanText(request.data.alias, 80);
    const deviceId = normalizeDeviceId(request.data.deviceId);
    const suppliedClaimCode = normalizeClaimCode(request.data.claimCode);

    const termsAccepted = request.data.termsAccepted === true;
    const termsVersion = cleanText(request.data.termsVersion, 40);
    const termsSha256 = cleanText(request.data.termsSha256, 64).toLowerCase();
    const acceptedAtClient = cleanText(request.data.termsAcceptedAtClient, 80);
    const termsPage = cleanText(request.data.termsPage, 160);
    const userAgent = cleanText(request.data.userAgent, 500);

    if (!termsAccepted) {
      throw new HttpsError(
        "failed-precondition",
        "You must accept the current AIDoser Terms before claiming a device."
      );
    }

    if (
      termsVersion !== CURRENT_TERMS_VERSION ||
      termsSha256 !== CURRENT_TERMS_SHA256
    ) {
      throw new HttpsError(
        "failed-precondition",
        "The Terms shown by this setup page are outdated. Refresh the page and try again."
      );
    }

    const claimRef = admin.database().ref(`/deviceClaims/${deviceId}`);

    const transaction = await claimRef.transaction((claim) => {
      if (!claim) {
        return;
      }

      if (claim.claimed === true || claim.claimedBy) {
        return;
      }

      const storedClaimCode = cleanText(
        claim.claimCode || claim.code,
        40
      ).toUpperCase();

      if (!storedClaimCode || storedClaimCode !== suppliedClaimCode) {
        return;
      }

      return {
        ...claim,
        claimed: true,
        claimedBy: uid,
        claimedAt: Date.now(),
        claimedEmail: email,
      };
    });

    if (!transaction.committed) {
      const currentClaim = (await claimRef.once("value")).val();

      if (!currentClaim) {
        throw new HttpsError(
          "not-found",
          "No claim record exists for that device ID."
        );
      }

      if (currentClaim.claimed === true || currentClaim.claimedBy) {
        throw new HttpsError(
          "already-exists",
          "This AIDoser has already been claimed."
        );
      }

      throw new HttpsError(
        "permission-denied",
        "The device ID and claim code do not match."
      );
    }

    const now = Date.now();
    const acceptedClaim = transaction.snapshot.val() || {};
    const deviceLabel = cleanText(acceptedClaim.label, 80);
    const requestIp = cleanText(
      request.rawRequest && request.rawRequest.ip,
      100
    );

    const agreementRecord = {
      accepted: true,
      version: CURRENT_TERMS_VERSION,
      sha256: CURRENT_TERMS_SHA256,
      acceptedAt: now,
      acceptedAtClient: acceptedAtClient || null,
      uid,
      email: email || null,
      customerName: customerName || null,
      deviceId,
      deviceLabel: deviceLabel || null,
      source: "customer-setup",
      termsPage: termsPage || null,
      userAgent: userAgent || null,
      ipAddress: requestIp || null,
    };

    const updates = {
      [`/users/${uid}/name`]: customerName,
      [`/users/${uid}/email`]: email,
      [`/users/${uid}/role`]: "customer",
      [`/users/${uid}/createdAt`]: now,
      [`/users/${uid}/devices/${deviceId}`]: true,

      [`/devices/${deviceId}/ownerUID`]: uid,
      [`/devices/${deviceId}/claimed`]: true,
      [`/devices/${deviceId}/claimedAt`]: now,

      [`/users/${uid}/agreements/${CURRENT_TERMS_VERSION}/${deviceId}`]: agreementRecord,
      [`/devices/${deviceId}/termsAcceptance`]: agreementRecord,
    };

    if (customerName) {
      updates[`/devices/${deviceId}/Name`] = customerName;
    }

    if (alias) {
      updates[`/devices/${deviceId}/alias`] = alias;
    }

    await admin.database().ref().update(updates);

    logger.info(
      `Device ${deviceId} claimed by UID ${uid} (${email || "no email"})`
    );

    return {
      ok: true,
      deviceId,
      alias,
      termsVersion: CURRENT_TERMS_VERSION,
      termsSha256: CURRENT_TERMS_SHA256,
    };
  }
);
// TEST ONLY
exports.testResetDevice = require("./testReset").testResetDevice;
