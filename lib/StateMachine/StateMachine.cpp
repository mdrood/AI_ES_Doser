#include "StateMachine.h"

StateManager::StateManager() {
    currentState = SystemState::BOOT;
}

void StateManager::transitionTo(SystemState newState) {
    if (currentState == newState) return;

    Serial.print("STATE CHANGE: ");
    Serial.print(getStateName());
    
    currentState = newState;
    
    Serial.print(" -> ");
    Serial.println(getStateName());

    // Logic: If we hit E-STOP, we should probably do something specific here
    if (newState == SystemState::EMERGENCY_STOP) {
        // We will eventually call doser.stopAll() here
    }
}


bool StateManager::canDose() {
    // Only allow dosing if we are in IDLE (waiting) or AUTO mode
    // Block dosing during Calibration or E-Stop
    return (currentState == SystemState::AUTO_DOSING || currentState == SystemState::IDLE);
}

String StateManager::getStateName() {
    switch (currentState) {
        case SystemState::BOOT:           return "BOOT";
        case SystemState::IDLE:           return "IDLE";
        case SystemState::AUTO_DOSING:    return "AUTO_DOSING";
        case SystemState::CALIBRATION:    return "CALIBRATION";
        case SystemState::EMERGENCY_STOP: return "EMERGENCY_STOP";
        default:                          return "UNKNOWN";
    }
}