#include <Arduino.h>
#include <WiFi.h>

static constexpr uint8_t PUMP_PINS[4] = {25, 26, 27, 22};
// Production burn-in: each pump runs 15 seconds, continuously cycling
// P1 -> P2 -> P3 -> P4 for a total of 5 minutes.
static constexpr unsigned long PUMP_ON_MS = 15000UL;
static constexpr unsigned long DEFAULT_BURNIN_MS = 300000UL;

String assignedDeviceId = "UNASSIGNED";
unsigned long burnInDurationMs = DEFAULT_BURNIN_MS;
unsigned long burnInStartedMs = 0;
unsigned long pumpStopMs = 0;
int currentPump = -1;
int nextPump = 0;
unsigned long completedRuns = 0;
bool testRunning = false;
bool testComplete = false;

void allPumpsOff() {
  for (int i = 0; i < 4; ++i) {
    pinMode(PUMP_PINS[i], OUTPUT);
    digitalWrite(PUMP_PINS[i], LOW);
  }
}

String chipIdString() {
  uint64_t chipid = ESP.getEfuseMac();
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%04X%08X",
           (uint16_t)(chipid >> 32), (uint32_t)chipid);
  return String(buffer);
}

void printIdentity() {
  Serial.println("AIDOSER_BURNIN_READY");
  Serial.printf("DEVICE ID: %s\n", assignedDeviceId.c_str());
  Serial.printf("MAC Address: %s\n", WiFi.macAddress().c_str());
  Serial.printf("Chip ID: %s\n", chipIdString().c_str());
  Serial.println("Board: vintlabs-devkit-v1");
}

void stopPump(const char* reason) {
  if (currentPump >= 0 && currentPump < 4) {
    digitalWrite(PUMP_PINS[currentPump], LOW);
    Serial.printf("BURNIN_PUMP_OFF pump=%d reason=%s elapsedMs=%lu\n",
                  currentPump + 1, reason, millis() - burnInStartedMs);
  }
  currentPump = -1;
  pumpStopMs = 0;
  allPumpsOff();
}

void startTest() {
  allPumpsOff();
  completedRuns = 0;
  nextPump = 0;
  currentPump = -1;
  burnInStartedMs = millis();
  testRunning = true;
  testComplete = false;
  Serial.printf("BURNIN_STARTED deviceId=%s durationMs=%lu pumpOnMs=%lu mode=continuous\n",
                assignedDeviceId.c_str(), burnInDurationMs,
                PUMP_ON_MS);
}

void printStatus() {
  unsigned long elapsed = burnInStartedMs ? millis() - burnInStartedMs : 0;
  unsigned long remaining = (testRunning && elapsed < burnInDurationMs)
                              ? burnInDurationMs - elapsed : 0;
  Serial.printf("BURNIN_STATUS deviceId=%s running=%d complete=%d currentPump=%d completedRuns=%lu elapsedMs=%lu remainingMs=%lu freeHeap=%u minHeap=%u\n",
                assignedDeviceId.c_str(), testRunning ? 1 : 0,
                testComplete ? 1 : 0, currentPump + 1, completedRuns,
                elapsed, remaining, ESP.getFreeHeap(), ESP.getMinFreeHeap());
}

void handleCommand(String command) {
  command.trim();
  if (!command.length()) return;

  if (command.startsWith("SET_DEVICE_ID ")) {
    assignedDeviceId = command.substring(14);
    assignedDeviceId.trim();
    Serial.printf("DEVICE_ID_SET %s\n", assignedDeviceId.c_str());
  } else if (command.startsWith("SET_DURATION_MIN ")) {
    // Production burn-in is always fixed at 5 minutes.
    burnInDurationMs = DEFAULT_BURNIN_MS;
    Serial.printf("BURNIN_DURATION_FIXED durationMs=%lu\n",
                  burnInDurationMs);
  } else if (command == "START") {
    startTest();
  } else if (command == "STOP") {
    stopPump("serial-stop");
    testRunning = false;
    Serial.println("BURNIN_STOPPED");
  } else if (command == "STATUS") {
    printStatus();
  } else if (command == "IDENTITY") {
    printIdentity();
  }
}

void setup() {
  allPumpsOff();
  Serial.begin(115200);
  delay(700);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  printIdentity();
}

void loop() {
  while (Serial.available()) {
    handleCommand(Serial.readStringUntil('\n'));
  }

  if (!testRunning) {
    allPumpsOff();
    delay(25);
    return;
  }

  unsigned long now = millis();
  unsigned long elapsed = now - burnInStartedMs;

  if (elapsed >= burnInDurationMs) {
    stopPump("complete");
    testRunning = false;
    testComplete = true;
    Serial.printf("BURNIN_COMPLETE result=PASS deviceId=%s completedRuns=%lu elapsedMs=%lu\n",
                  assignedDeviceId.c_str(), completedRuns, elapsed);
    return;
  }

  if (currentPump >= 0 && (long)(now - pumpStopMs) >= 0) {
    stopPump("normal-end");
    completedRuns++;
    nextPump = (nextPump + 1) % 4;
  }

  // Start the next pump immediately after the previous one turns off.
  // allPumpsOff() is called before every start, so pumps never overlap.
  if (currentPump < 0) {
    allPumpsOff();
    currentPump = nextPump;
    digitalWrite(PUMP_PINS[currentPump], HIGH);
    pumpStopMs = now + PUMP_ON_MS;
    Serial.printf("BURNIN_PUMP_ON pump=%d pin=%d runMs=%lu elapsedMs=%lu\n",
                  currentPump + 1, PUMP_PINS[currentPump],
                  PUMP_ON_MS, elapsed);
  }

  static unsigned long lastStatusMs = 0;
  if (now - lastStatusMs >= 60000UL) {
    lastStatusMs = now;
    printStatus();
  }

  delay(25);
}