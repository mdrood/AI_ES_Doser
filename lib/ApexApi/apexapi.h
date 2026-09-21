#pragma once
#include <Arduino.h>

class ApexApi {
public:
  ApexApi();
  void setIpAddr(String ip);
  String getState();

  float getTempF();
  float getPh();
  float getCond();
  float getAlk();
  float getCa();
  float getMg();
  // Added 2026-09-12: real, structural fix -- Apex's own reported test
  // timestamp was already being parsed into `latest.date` internally
  // (see apexapi.cpp), but never exposed through the public interface,
  // so the real acceptance logic in man.cpp had no way to use it -- it
  // fell back to comparing raw chemistry values instead, a genuinely
  // fragile approach if Trident updates its probes sequentially rather
  // than atomically (a probe caught mid-update could produce a partial,
  // inconsistent snapshot that gets accepted as a new baseline, causing
  // the TRUE completed result moments later to look like no change at
  // all). The emulator path already uses exactly this date-based
  // approach (see acceptNewTridentMeasurement's testTime parameter) --
  // this brings the real Apex path up to the same, already-proven
  // pattern instead of continuing to rely on the weaker fallback.
  String getDate();

  void setTempF(float v);
  void setPh(float v);
  void setCond(float v);
  void setAlk(float v);
  void setCa(float v);
  void setMg(float v);
  bool getLightStatus();

  bool apexJson = true;   // set true to use status.json

private:
  String apexIp;
  //String apexIp = "192.168.4.68";
  float tempF = NAN;
  float ph    = NAN;
  float cond  = NAN;
  float alk   = NAN;
  float ca    = NAN;
  float mg    = NAN;
};