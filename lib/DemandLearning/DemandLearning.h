#pragma once

// The 7-day rolling Alk/Calcium demand learner used to live inline in
// main.cpp. It's a self-contained subsystem: it persists daily chemistry
// averages to LittleFS, computes a recommended baseline dosing adjustment,
// and publishes its status to Firebase. Moved here unchanged - just the
// file it lives in changed, not what it does.

void loadAlkDemandLearningSetting();
void saveAlkDemandLearningSetting();
void loadCalciumDemandLearningSetting();
void saveCalciumDemandLearningSetting();

bool saveAlkDemandHistory();
void loadAlkDemandHistory();
void printAlkDemandRecommendation(const char* source);
bool publishAlkDemandStatusToFirebase(const char* source, bool force);
void recordCompletedDayAndLearn(float avgAlk);

bool saveCalciumDemandHistory();
void loadCalciumDemandHistory();
void printCalciumDemandRecommendation(const char* source);
bool publishCalciumDemandStatusToFirebase(const char* source, bool force);
void recordCompletedCalciumDayAndLearn(float avgCa);
