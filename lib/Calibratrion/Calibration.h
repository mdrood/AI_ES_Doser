#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <Arduino.h>
#include <Preferences.h>
#include "Doser.h"

class CalibrationManager {
public:
    CalibrationManager(Doser& doserRef);
    void begin();
    
    // Starts physical pump for calibration
    void startCalibrationRun(int pumpIndex, uint32_t durationMs);
    
    // Saves the volume measured by user to NVS and Firebase
    void finalizeCalibration(int pumpIndex, float measuredMl, String devID);

private:
    Doser& _doser;
    Preferences _prefs;
};

#endif