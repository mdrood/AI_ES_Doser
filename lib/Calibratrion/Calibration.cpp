#include "calibration.h"
#include <FirebaseESP32.h>

extern FirebaseData fbdo; 

CalibrationManager::CalibrationManager(Doser& doserRef) : _doser(doserRef) {}

void CalibrationManager::begin() {
    _prefs.begin("cal", false);
    for(int i=0; i<6; i++) {
        float rate = _prefs.getFloat(("p" + String(i)).c_str(), 45.0f);
        // We know setCalibration works because the compiler suggested it earlier
        _doser.setCalibration(i, rate); 
    }
    _prefs.end();
}

void CalibrationManager::startCalibrationRun(int pumpIndex, uint32_t durationMs) {
    Serial.printf("STARTING CALIBRATION: Pump %d\n", pumpIndex + 1);
    
    // We use doseMl since we saw it in your main.cpp. 
    // Telling it to dose a huge amount effectively turns it "ON"
    _doser.doseMl(pumpIndex, 5000.0f); 
    
    delay(durationMs);
    
    // Stop the pump by telling it to dose 0
    _doser.doseMl(pumpIndex, 0.0f);
    
    Serial.println("CALIBRATION RUN COMPLETE.");
}

void CalibrationManager::finalizeCalibration(int pumpIndex, float measuredMl, String devID) {
    _prefs.begin("cal", false);
    _prefs.putFloat(("p" + String(pumpIndex)).c_str(), measuredMl);
    _prefs.end();

    _doser.setCalibration(pumpIndex, measuredMl);

    String path = "/devices/" + devID + "/calibration/results/p" + String(pumpIndex + 1);
    Firebase.setFloat(fbdo, path.c_str(), measuredMl);
}