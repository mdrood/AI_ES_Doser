#include "StateMachine.h"
#include "logger.h"

// Added: this file previously only ever printed state transitions to
// Serial. On a device someone has a USB cable plugged into, that's fine --
// but on a remote customer unit with no local serial access, a
// SystemState transition (including into PROVISIONING, which silently
// disables the entire dosing scheduler downstream in main.cpp) was
// completely invisible in the uploaded logs. Confirmed directly: searched
// a live customer device's entire multi-week log archive for "STATE
// CHANGE" and found zero matches, even though transitions almost
// certainly occurred -- because this line never went anywhere the
// customer's logger could pick it up. logger is the same global instance
// main.cpp already uses (defined in lib/Logger/logger.cpp); referencing
// it here the same minimal way main.cpp references dailyDoseTotals from
// Doser.cpp, now via the real header instead of an incomplete forward
// declaration (that first attempt failed to compile -- forward-declaring
// a class doesn't tell the compiler it has print()/println() methods).
extern Logger logger;

StateManager::StateManager() {
    currentState = SystemState::BOOT;
}

void StateManager::transitionTo(SystemState newState) {
    if (currentState == newState) return;

    String fromName = getStateName();

    Serial.print("STATE CHANGE: ");
    Serial.print(fromName);
    
    currentState = newState;
    
    String toName = getStateName();
    Serial.print(" -> ");
    Serial.println(toName);

    logger.print("STATE CHANGE: ");
    logger.print(fromName);
    logger.print(" -> ");
    logger.println(toName);

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
        // Added: main.cpp transitions into this state (see its WiFi-connect
        // fallback in setup()), but it had no case here, so any transition
        // into or out of PROVISIONING printed as "UNKNOWN" -- exactly the
        // state this whole investigation has been trying to confirm or
        // rule out, previously unreadable in its own transition log.
        case SystemState::PROVISIONING:   return "PROVISIONING";
        default:                          return "UNKNOWN";
    }
}