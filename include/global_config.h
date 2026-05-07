#ifndef GLOBAL_CONFIG_H
#define GLOBAL_CONFIG_H

#include <Arduino.h>

// --- SYSTEM STATE ---
extern bool emergencyStop;
extern int dosingMode;

// --- APEX LIVE DATA (The Full Mirror) ---
extern float currentTempF;
extern float currentPh;
extern float currentCond; 
extern float currentPpt;  
extern float currentSg;   
extern float currentAlk;  // From Trident/Apex
extern float currentCa;   // From Trident/Apex
extern float currentMg;   // From Trident/Apex
extern String lastApexDate;

// --- MANUAL TEST ENTRIES (The "Override" Truth) ---
struct ManualTest {
    float alk = 8.5;
    float ca = 420.0;
    float mg = 1350.0;
    float ph = 8.2;
    uint32_t timestamp = 0;
};
extern ManualTest lastLocalTest;

// --- CALIBRATION TRUTH ---
extern float pumpFlowRates[4]; 



#endif