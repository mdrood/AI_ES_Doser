#pragma once

#include <Arduino.h>

struct ApexEmuChemistry {
  bool valid = false;
  float alk = NAN;
  float ca  = NAN;
  float mg  = NAN;
  float ph  = NAN;
  String source = "apex-log-emulator";
  String deviceId = "";
  String logTime = "";
  String error = "";
};

class ApexLogEmulator {
public:
  ApexLogEmulator();

  // scriptUrl example:
  // https://script.google.com/macros/s/AKfycbxxxx/exec?device=reefDoser2
  void begin(const String& scriptUrl, unsigned long pollMs = 60000);

  // Call from loop(). Returns true only when fresh valid values were updated.
  bool loop();

  // Force a poll now. Returns true if valid values were received.
  bool pollNow();

  const ApexEmuChemistry& chemistry() const;
  bool hasValidChemistry() const;
  unsigned long lastPollMs() const;
  unsigned long lastGoodMs() const;

private:
  String _scriptUrl;
  unsigned long _pollIntervalMs = 60000;
  unsigned long _lastPollMs = 0;
  unsigned long _lastGoodMs = 0;
  ApexEmuChemistry _chem;
};
