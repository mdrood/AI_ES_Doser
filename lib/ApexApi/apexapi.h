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
  // Apex's own reported test timestamp for the latest record (raw string,
  // whatever format that record used -- epoch-seconds-as-string for the
  // JSON path, or the XML <date> text for the XML path). main.cpp uses
  // this to tell a genuinely new Trident test apart from a repeated poll
  // of the same unchanged result -- restored here because the project's
  // main.cpp (acceptNewTridentMeasurement()'s caller in syncAllTruths())
  // already calls apex.getDate() and will fail to link without it.
  String getDate();

  void setTempF(float v);
  void setPh(float v);
  void setCond(float v);
  void setAlk(float v);
  void setCa(float v);
  void setMg(float v);
  void setDate(String v);
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
  String dateStr;
};