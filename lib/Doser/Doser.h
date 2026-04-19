#ifndef DOSER_H
#define DOSER_H

#include <Arduino.h>

struct PumpChannel {
    uint8_t pin;
    float mlPerMin;
    unsigned long runUntil;
    bool isActive;
};

class Doser {
public:
    Doser();
    void begin();
    void doseMl(int pumpIdx, float ml); 
    void tick();
    bool isPumpRunning(int pumpIdx);

    // --- NEW CALIBRATION FUNCTIONS ---
    void setCalibration(int pumpIdx, float newMlPerMin);
    void startManualRun(int pumpIdx);
    void stopManualRun(int pumpIdx);

private:
    PumpChannel _pumps[4]; 
};

#endif