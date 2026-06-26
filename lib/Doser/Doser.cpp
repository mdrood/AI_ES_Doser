#include "Doser.h"
#include <Preferences.h>

Doser::Doser() {
    // Initialize pins (Update these to your actual ESP32 pins)
    _pumps[0] = {22, 60.0, 0, false}; // Pump 1
    _pumps[1] = {25, 60.0, 0, false}; // Pump 2
    _pumps[2] = {26, 60.0, 0, false}; // Pump 3
    _pumps[3] = {27, 60.0, 0, false}; // Pump 4
}

// Add this near the top of Doser.cpp
float dailyDoseTotals[4] = {0, 0, 0, 0};

#include <Preferences.h>

void Doser::begin() {
    Preferences prefs;
    prefs.begin("doser-settings", true); // Open in Read-Only mode

    _pumps[0].pin = 22;
    _pumps[1].pin = 25;
    _pumps[2].pin = 26;
    _pumps[3].pin = 27;

    // Load the same NVS keys used by main.cpp/saveFlowRate().
    // The old p0/p1/p2/p3 keys are kept as fallback for backward compatibility.
    _pumps[0].mlPerMin = prefs.getFloat("flow_p1", prefs.getFloat("p0", 60.0));
    _pumps[1].mlPerMin = prefs.getFloat("flow_p2", prefs.getFloat("p1", 60.0));
    _pumps[2].mlPerMin = prefs.getFloat("flow_p3", prefs.getFloat("p2", 60.0));
    _pumps[3].mlPerMin = prefs.getFloat("flow_p4", prefs.getFloat("p3", 60.0));

    prefs.end();

    for (int i = 0; i < 4; i++) {
        pinMode(_pumps[i].pin, OUTPUT);
        digitalWrite(_pumps[i].pin, LOW);
    }
}

float Doser::doseMl(int i, float ml) {
    if (i < 0 || i > 3 || ml <= 0) return 0.0f;
    if (_pumps[i].mlPerMin <= 0.0f) return 0.0f;

    float requestedMl = ml;
    float secondsToRun = (requestedMl / _pumps[i].mlPerMin) * 60.0f;

    // SAFETY CAP: Never let a pump run for more than 60 seconds in a single dose.
    // Return the actual mL that this capped runtime can deliver so daily totals,
    // reservoir levels, and Firebase history do not over-count large requests.
    if (secondsToRun > 60.0f) {
        secondsToRun = 60.0f;
    }

    float actualMl = _pumps[i].mlPerMin * (secondsToRun / 60.0f);

    _pumps[i].runUntil = millis() + (unsigned long)(secondsToRun * 1000.0f);
    _pumps[i].isActive = true;
    digitalWrite(_pumps[i].pin, HIGH);

    return actualMl;
}

void Doser::tick() {
    unsigned long now = millis();
    for (int i = 0; i < 4; i++) {
        if (_pumps[i].isActive && now >= _pumps[i].runUntil) {
            digitalWrite(_pumps[i].pin, LOW);
            _pumps[i].isActive = false;
        }
    }
}

void Doser::setCalibration(int i, float newMlPerMin) {
    if (i >= 0 && i < 4) {
        _pumps[i].mlPerMin = newMlPerMin;
    }
}

void Doser::startManualRun(int i) {
    if (i < 0 || i > 3) return;
    digitalWrite(_pumps[i].pin, HIGH);
    _pumps[i].isActive = true;
    _pumps[i].runUntil = millis() + 3600000; // 1-hour safety cap
}

void Doser::stopManualRun(int i) {
    if (i < 0 || i > 3) return;
    digitalWrite(_pumps[i].pin, LOW);
    _pumps[i].isActive = false;
}

bool Doser::isPumpRunning(int i) {
    if (i >= 0 && i < 4) {
        return _pumps[i].isActive;
    }
    return false;
}

//TODO stop pumps
void Doser::stopAllPumps(){

}

float Doser::getTotalDosedToday() {
    return _dailyTotalMl;
}

void Doser::resetDailyTotal() {
    _dailyTotalMl = 0.0f;
}