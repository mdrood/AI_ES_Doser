#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// WiFi recovery hotspot.
//
// Problem: a tank whose router changed (same network name, new password) kept
// retrying the old saved credentials forever and the customer had no way to
// enter the new one without a USB cable.
//
// Behavior:
//   * First 2 minutes without WiFi: nothing new, normal reconnect attempts.
//   * After 2 minutes: open the "AIDoser-<deviceID>" setup hotspot (the WiFi
//     radio runs station + hotspot together, so reconnect attempts continue).
//   * Saved credentials are NEVER erased automatically.
//   * New credentials typed into the setup page are tried first and saved
//     (once) only after they actually connect.
//   * When WiFi is back, the hotspot shuts itself off.
//   * Nothing here blocks, changes SystemState, or touches dosing.
//
// Hooks in main.cpp: wifiRecoveryBegin() once in setup() right after
// registerWebRoutes(); wifiRecoveryTick() every pass of loop().
// ---------------------------------------------------------------------------
void wifiRecoveryBegin();
void wifiRecoveryTick();
bool wifiRecoveryHotspotActive();
// P-3.0.26: open the setup hotspot on request (status button held 5 s) for N minutes,
// even while WiFi is working. Safe to call from loop(); saved WiFi is kept.
void wifiRecoveryRequestHotspot(uint32_t minutes);
