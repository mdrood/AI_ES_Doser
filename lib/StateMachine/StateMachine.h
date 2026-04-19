#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#include <Arduino.h>

enum class SystemState {
    BOOT,
    IDLE,
    AUTO_DOSING,
    CALIBRATION,
    PROVISIONING,
    EMERGENCY_STOP
};

class StateManager {
public:
    StateManager();
    
    void transitionTo(SystemState newState);
    SystemState getCurrentState() {
        return currentState;
    }
    bool canDose();
    String getStateName(); // Helpful for the Reporting Lib

    SystemState currentState;
};

#endif