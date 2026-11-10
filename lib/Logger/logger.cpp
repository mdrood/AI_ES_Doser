#include "logger.h"
#include <time.h>
#include <stdarg.h>
#include <esp_attr.h>
#include <esp_system.h>

Logger logger;

// Architecture goals:
// 1. Never allow the logger queue to grow without limit.
// 2. Attempt only one queued upload per cycle.
// 3. A failed/stuck file cannot block every later file forever.
// 4. Keep network blocking bounded with short timeouts.
// 5. Logger failure must never stop dosing/main-loop execution.

static const uint32_t LOGGER_FORCED_UPLOAD_EVERY_MS = 30UL * 60UL * 1000UL;
static const size_t LOGGER_MAX_SAFE_GET_BYTES = 4UL * 1024UL;
static const uint8_t LOGGER_MAX_QUEUED_FILES = 5;
// Failures due to network/TLS exhaustion must never delete evidence.
static const uint8_t LOGGER_MAX_FAILED_RETRIES = 2; // diagnostic threshold only
static const uint16_t LOGGER_HTTP_TIMEOUT_MS = 8000;

// Boot-record files ("bootrec_*.log") are a separate, small namespace from
// the ordinary "queued_*.log" pool and never count against
// LOGGER_MAX_QUEUED_FILES. Capped independently so a crash-loop can't let
// this grow unbounded; normally drained (uploaded + deleted) almost
// immediately since _uploadNextQueuedFile() now prefers these first.
static const uint8_t LOGGER_MAX_BOOT_FILES = 3;


// ===========================================================================
// Stage trace (reset diagnosis)
// ===========================================================================
struct TraceSlot {
  char name[20];
  uint32_t startMs;
};
struct TraceRec {
  uint32_t magicA;
  TraceSlot slot[2];
  uint32_t aliveMs;
  uint32_t magicB;
};
static const uint32_t TRACE_MAGIC_A = 0x54524331UL;
static const uint32_t TRACE_MAGIC_B = 0xABAD1234UL;
static RTC_NOINIT_ATTR TraceRec g_trace;
static volatile bool g_traceReady = false;

static bool traceValid(const TraceRec& r) {
  return r.magicA == TRACE_MAGIC_A && r.magicB == TRACE_MAGIC_B;
}

void traceStage(uint8_t slot, const char* name) {
  if (slot > 1 || !g_traceReady) return;
  g_trace.slot[slot].startMs = millis();
  strlcpy(g_trace.slot[slot].name, name ? name : "?", sizeof(g_trace.slot[slot].name));
}

String traceTakePreviousBootSummary() {
  String out = "";
  if (traceValid(g_trace)) {
    TraceRec prev = g_trace;   // copy before we reinitialise
    const char* labels[2] = {"loop", "upload"};
    out = "PREVIOUS RUN last stages:";
    for (int i = 0; i < 2; i++) {
      prev.slot[i].name[sizeof(prev.slot[i].name) - 1] = 0;
      bool idle = (prev.slot[i].name[0] == 0) || strcmp(prev.slot[i].name, "idle") == 0;
      out += " ";
      out += labels[i];
      out += "=";
      if (idle) {
        out += "idle";
      } else {
        uint32_t ranMs = (prev.aliveMs >= prev.slot[i].startMs) ? (prev.aliveMs - prev.slot[i].startMs) : 0;
        out += String(prev.slot[i].name) + "(running>=" + String((unsigned long)(ranMs / 1000UL)) + "s)";
      }
    }
    out += " lastAliveUptime=" + String((unsigned long)(prev.aliveMs / 1000UL)) + "s";
  } else {
    out = "PREVIOUS RUN last stages: unavailable (power loss or first boot)";
  }
  memset(&g_trace, 0, sizeof(g_trace));
  g_trace.magicA = TRACE_MAGIC_A;
  g_trace.magicB = TRACE_MAGIC_B;
  g_trace.aliveMs = millis();
  g_traceReady = true;
  return out;
}

extern bool otaInProgress;   // main.cpp

static void traceWatcherTask(void* arg) {
  (void)arg;
  uint32_t reportedStart[2] = {0, 0};
  const uint32_t warnMs[2] = {12000UL, 45000UL};
  const char* labels[2] = {"loop", "upload"};
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint32_t now = millis();
    g_trace.aliveMs = now;
    for (int i = 0; i < 2; i++) {
      TraceSlot s = g_trace.slot[i];
      s.name[sizeof(s.name) - 1] = 0;
      if (s.name[0] == 0 || strcmp(s.name, "idle") == 0) continue;
      // The loop task can enter a new stage between the millis() sample above
      // and this copy, making startMs a few ms NEWER than 'now'. Unsigned
      // subtraction then wraps to ~4294967 s and a false "STUCK" report fires.
      // Skip that case; the next tick sees the true value.
      int32_t ranSigned = (int32_t)(now - s.startMs);
      if (ranSigned < 0) continue;
      uint32_t ran = (uint32_t)ranSigned;
      // A long OTA download is normal and writes flash itself; do not add
      // log/flash writes of our own while it runs. The RTC stage record is
      // still updated, so a reset during OTA is still named at next boot.
      if (otaInProgress) continue;
      if (ran >= warnMs[i] && reportedStart[i] != s.startMs) {
        reportedStart[i] = s.startMs;
        Serial.printf("STUCK: %s task in stage '%s' for %lu s (uptime %lu s, heap %u, minHeap %u)\n",
                      labels[i], s.name, (unsigned long)(ran / 1000UL),
                      (unsigned long)(now / 1000UL), (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMinFreeHeap());
        logger.printf("STUCK: %s task in stage '%s' for %lu s (uptime %lu s, heap %u, minHeap %u)\n",
                      labels[i], s.name, (unsigned long)(ran / 1000UL),
                      (unsigned long)(now / 1000UL), (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMinFreeHeap());
      }
    }
  }
}

void traceStartWatcher() {
  static bool started = false;
  if (started) return;
  started = true;
  // Priority 2 on core 0: runs even if loopTask is stuck, and never waits on it.
  // 6 KB stack: the STUCK report path calls Serial/WebSerial/logger.printf
  // (LittleFS file write), which overflowed the old 3 KB and caused a
  // "Stack canary watchpoint triggered (traceWatch)" panic.
  xTaskCreatePinnedToCore(traceWatcherTask, "traceWatch", 6144, nullptr, 2, nullptr, 0);
}

// Shared with main.cpp: while true, main.cpp's secure-connection gate
// (apexTlsReservedOrCoolingDown) reports busy so Firebase/Apex TLS work
// never overlaps the upload task's TLS session.
extern volatile bool loggerTlsActive;
extern volatile unsigned long apexTlsCooldownUntilMs;

// P-3.0.28: WebSerial (the async web socket behind the browser console) is not
// safe to write from two tasks at once. The log upload runs on its own task and
// prints status lines while loop() prints too, and 3 of the 4 customer-unit
// panics so far were 5-14 s into a log upload. Only the main (loop) task now
// writes to WebSerial; any other task still writes to Serial and the log file,
// so nothing is lost from the uploaded logs.
static TaskHandle_t g_loggerMainTask = nullptr;   // set in Logger::begin() (runs in setup(), i.e. loopTask)
static inline bool webSerialAllowedHere() {
  return g_loggerMainTask == nullptr || xTaskGetCurrentTaskHandle() == g_loggerMainTask;
}

static void loggerStatus(const String& message) {
  Serial.println(message);
  if (webSerialAllowedHere()) WebSerial.println(message);
}

Logger::Logger()
  : _uploadEveryMs(LOGGER_UPLOAD_EVERY_MS),
    _rotateBytes(LOGGER_ROTATE_BYTES),
    _lastUploadAttemptMs(0),
    _enabled(false),
    _fsReady(false),
    _uploadBusy(false),
    _uploadTaskActive(false),
    _failedPath(""),
    _failedCount(0),
    _bootRecordPath("") {}

bool Logger::begin(const String& deviceId, const String& appsScriptUrl) {
  return begin(deviceId,
               appsScriptUrl,
               "",
               LOGGER_UPLOAD_EVERY_MS,
               LOGGER_ROTATE_BYTES);
}

bool Logger::begin(const String& deviceId,
                   const String& appsScriptUrl,
                   const String& apiKey,
                   uint32_t uploadEveryMs,
                   size_t rotateBytes) {
  g_loggerMainTask = xTaskGetCurrentTaskHandle();
  _deviceId = deviceId;
  _appsScriptUrl = appsScriptUrl;
  _apiKey = apiKey;

  // Keep the tested 30-minute production interval.
  (void)uploadEveryMs;
  _uploadEveryMs = LOGGER_FORCED_UPLOAD_EVERY_MS;

  // The current Apps Script receives the full log in a GET URL.
  // Force 4 KB files so URL encoding stays within a safe range.
  (void)rotateBytes;
  // Rotate BELOW the upload limit. A chunk is rotated once it has reached this
  // size, so it ends up this size plus whatever the last write added. Rotating
  // at exactly the upload limit produced 4114-4162 byte chunks, which
  // _uploadOneFile() then discarded as "unusable" (> LOGGER_MAX_SAFE_GET_BYTES):
  // ordinary log chunks could never be uploaded, only small boot records.
  _rotateBytes = LOGGER_MAX_SAFE_GET_BYTES - 1024;

  _enabled = true;
  _uploadBusy = false;
  _failedPath = "";
  _failedCount = 0;
  _bootRecordPath = ""; // fresh boot -> lazily create a new bootrec_ file

  if (!LittleFS.begin(true)) {
    loggerStatus("LOGGER: LittleFS mount failed; Drive logging disabled.");
    _fsReady = false;
    return false;
  }

  _fsReady = true;
  _lastUploadAttemptMs = millis();

  if (!LittleFS.exists("/logs")) {
    LittleFS.mkdir("/logs");
  }

  // Do not erase the queue at boot. Just trim it safely to the newest five
  // (ordinary queue) and newest three (boot-record files), independently.
  _enforceQueueLimit();
  _enforceBootFileLimit();

  println("LOGGER: started fail-safe local-first Drive logger");
  printf("LOGGER: interval=%lu ms rotate=%u bytes maxQueue=%u maxBootFiles=%u timeout=%u ms retries=%u\n",
         (unsigned long)_uploadEveryMs,
         (unsigned)_rotateBytes,
         (unsigned)LOGGER_MAX_QUEUED_FILES,
         (unsigned)LOGGER_MAX_BOOT_FILES,
         (unsigned)LOGGER_HTTP_TIMEOUT_MS,
         (unsigned)LOGGER_MAX_FAILED_RETRIES);

  return true;
}

void Logger::setEnabled(bool enabled) {
  _enabled = enabled;
}

bool Logger::isEnabled() const {
  return _enabled && _fsReady;
}

void Logger::setUploadIntervalMs(uint32_t uploadEveryMs) {
  (void)uploadEveryMs;
  _uploadEveryMs = LOGGER_FORCED_UPLOAD_EVERY_MS;
}

uint32_t Logger::getUploadIntervalMs() const {
  return _uploadEveryMs;
}

String Logger::_currentPath() const {
  return "/logs/current.log";
}

String Logger::_queuePath(uint32_t stamp) const {
  return "/logs/queued_" + String(stamp) + ".log";
}

// BEGIN seq-helper
// File names carry a zero-padded sequence number so that plain text ordering
// IS chronological ordering, and the number continues from the highest one
// already on flash so the order also survives reboots. (The old names used
// uptime seconds: "queued_18_..." sorted before "queued_9_...", and every
// reboot restarted at zero, so the NEWEST file was deleted as "oldest".)
// Old-style names ("queued_<digit>...") never match the new prefix and sort
// before it, so any leftovers are treated as oldest and drain out first.
static uint32_t nextFileSeq(const char* prefix, uint32_t& counter, bool& inited) {
  if (!inited) {
    inited = true;
    uint32_t maxSeen = 0;
    bool any = false;
    size_t plen = strlen(prefix);

    File root = LittleFS.open("/logs");
    if (root && root.isDirectory()) {
      File f = root.openNextFile();
      while (f) {
        String p = f.path();
        f.close();
        int at = p.indexOf(prefix);
        if (at >= 0 && p.endsWith(".log")) {
          uint32_t n = (uint32_t)strtoul(p.c_str() + at + plen, nullptr, 10);
          if (!any || n > maxSeen) maxSeen = n;
          any = true;
        }
        f = root.openNextFile();
      }
      root.close();
    }
    if (any) counter = maxSeen + 1;
  }
  return counter++;
}
// END seq-helper

String Logger::_nextQueuePath() const {
  static uint32_t counter = 0;
  static bool inited = false;

  uint32_t seq = nextFileSeq("queued_s", counter, inited);
  char buf[40];
  snprintf(buf, sizeof(buf), "/logs/queued_s%08lu.log", (unsigned long)seq);
  return String(buf);
}

String Logger::_nextBootRecordPath() const {
  static uint32_t counter = 0;
  static bool inited = false;

  // Separate "bootrec_" prefix keeps this out of every "queued_" scan in
  // _findOldestQueuedFile()/_enforceQueueLimit() -- it is never counted,
  // never evicted, and never picked up by the ordinary upload path.
  uint32_t seq = nextFileSeq("bootrec_s", counter, inited);
  char buf[40];
  snprintf(buf, sizeof(buf), "/logs/bootrec_s%08lu.log", (unsigned long)seq);
  return String(buf);
}

String Logger::_timestampPrefix() const {
  struct tm timeinfo;

  if (getLocalTime(&timeinfo, 10)) {
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String("[") + buffer + "] ";
  }

  return String("[ms=") + String(millis()) + "] ";
}

void Logger::print(const String& msg) {
  _writeRaw(msg, false);
}

void Logger::print(const char* msg) {
  _writeRaw(String(msg ? msg : ""), false);
}

void Logger::println(const String& msg) {
  _writeRaw(msg, true);
}

void Logger::println(const char* msg) {
  _writeRaw(String(msg ? msg : ""), true);
}

void Logger::println() {
  _writeRaw("", true);
}

void Logger::printf(const char* fmt, ...) {
  if (!fmt) return;

  char stackBuffer[256];

  va_list args;
  va_start(args, fmt);
  int needed = vsnprintf(stackBuffer, sizeof(stackBuffer), fmt, args);
  va_end(args);

  if (needed < 0) return;

  if ((size_t)needed < sizeof(stackBuffer)) {
    _writeRaw(String(stackBuffer), false);
    return;
  }

  char* heapBuffer = (char*)malloc((size_t)needed + 1);
  if (!heapBuffer) {
    loggerStatus("LOGGER: printf allocation failed; line skipped.");
    return;
  }

  va_start(args, fmt);
  vsnprintf(heapBuffer, (size_t)needed + 1, fmt, args);
  va_end(args);

  _writeRaw(String(heapBuffer), false);
  free(heapBuffer);
}

void Logger::log(const String& msg) {
  println(msg);
}

void Logger::log(const char* msg) {
  println(msg);
}

void Logger::logBootRecord(const String& line) {
  // Always echo locally, same as every other logger output path.
  Serial.println(line);
  if (webSerialAllowedHere()) WebSerial.println(line);

  if (!_enabled || !_fsReady) return;

  if (_bootRecordPath.length() == 0) {
    _bootRecordPath = _nextBootRecordPath();
  }

  File file = LittleFS.open(_bootRecordPath, FILE_APPEND);
  if (!file) {
    if (!LittleFS.exists("/logs")) {
      LittleFS.mkdir("/logs");
    }
    file = LittleFS.open(_bootRecordPath, FILE_WRITE);
    if (!file) {
      loggerStatus("LOGGER ERR: boot record write failed; continuing without it.");
      return;
    }
  }

  file.print(_timestampPrefix());
  file.println(line);
  file.close();

  // This file is small and bounded (a handful of short banner lines), so
  // there is no rotation to worry about -- just keep the total number of
  // retained boot-record files capped.
  _enforceBootFileLimit();
}

void Logger::_writeRaw(const String& text, bool addNewline) {
  const bool webOk = webSerialAllowedHere();
  if (addNewline) {
    Serial.println(text);
    if (webOk) WebSerial.println(text);
  } else {
    Serial.print(text);
    if (webOk) WebSerial.print(text);
  }

  if (!_enabled || !_fsReady) return;

  File file = LittleFS.open(_currentPath(), FILE_APPEND);

  if (!file) {
    if (!LittleFS.exists("/logs")) {
      LittleFS.mkdir("/logs");
    }

    file = LittleFS.open(_currentPath(), FILE_WRITE);
    if (!file) {
      loggerStatus("LOGGER ERR: current.log write failed; continuing without file log.");
      return;
    }
  }

  if (addNewline) {
    file.print(_timestampPrefix());
    file.println(text);
  } else {
    file.print(text);
  }

  file.close();
  _rotateIfNeeded();
}

void Logger::_rotateIfNeeded() {
  if (!_fsReady) return;

  File file = LittleFS.open(_currentPath(), FILE_READ);
  if (!file) return;

  size_t size = file.size();
  file.close();

  if (size < _rotateBytes) return;

  _queueCurrentLog();
  _enforceQueueLimit();
}

void Logger::_queueCurrentLog() {
  if (!_fsReady || !LittleFS.exists(_currentPath())) return;

  File file = LittleFS.open(_currentPath(), FILE_READ);
  if (!file) return;

  size_t size = file.size();
  file.close();

  if (size == 0) return;

  String queuedPath = _nextQueuePath();

  if (!LittleFS.rename(_currentPath(), queuedPath)) {
    loggerStatus("LOGGER ERR: could not queue current.log; continuing.");
    return;
  }

  loggerStatus("LOGGER: queued current log as " + queuedPath);
}

String Logger::_findOldestQueuedFile() {
  String oldest = "";

  File root = LittleFS.open("/logs");
  if (!root || !root.isDirectory()) return oldest;

  File file = root.openNextFile();

  while (file) {
    String path = file.path();
    if (path.startsWith("/littlefs")) {
      path.replace("/littlefs", "");
    }

    bool queued = path.indexOf("queued_") >= 0 && path.endsWith(".log");
    file.close();

    if (queued && (oldest.length() == 0 || path < oldest)) {
      oldest = path;
    }

    file = root.openNextFile();
    yield();
  }

  root.close();
  return oldest;
}

void Logger::_enforceQueueLimit() {
  if (!_fsReady) return;
  if (_uploadTaskActive) return;   // never delete files while the upload task reads one

  while (true) {
    uint8_t queuedCount = 0;

    File root = LittleFS.open("/logs");
    if (!root || !root.isDirectory()) return;

    File file = root.openNextFile();

    while (file) {
      String path = file.path();
      if (path.startsWith("/littlefs")) {
        path.replace("/littlefs", "");
      }

      if (path.indexOf("queued_") >= 0 && path.endsWith(".log")) {
        queuedCount++;
      }

      file.close();
      file = root.openNextFile();
      yield();
    }

    root.close();

    if (queuedCount <= LOGGER_MAX_QUEUED_FILES) return;

    String oldest = _findOldestQueuedFile();
    if (oldest.length() == 0) return;

    _clearFailure(oldest);

    if (LittleFS.remove(oldest)) {
      loggerStatus("LOGGER: queue over five; deleted oldest " + oldest);
    } else {
      loggerStatus("LOGGER ERR: failed deleting oldest queued file " + oldest);
      return;
    }
  }
}

String Logger::_findOldestBootFile() {
  String oldest = "";

  File root = LittleFS.open("/logs");
  if (!root || !root.isDirectory()) return oldest;

  File file = root.openNextFile();

  while (file) {
    String path = file.path();
    if (path.startsWith("/littlefs")) {
      path.replace("/littlefs", "");
    }

    bool isBootRec = path.indexOf("bootrec_") >= 0 && path.endsWith(".log");
    file.close();

    if (isBootRec && (oldest.length() == 0 || path < oldest)) {
      oldest = path;
    }

    file = root.openNextFile();
    yield();
  }

  root.close();
  return oldest;
}

void Logger::_enforceBootFileLimit() {
  if (_uploadTaskActive) return;
  if (!_fsReady) return;

  while (true) {
    uint8_t bootFileCount = 0;

    File root = LittleFS.open("/logs");
    if (!root || !root.isDirectory()) return;

    File file = root.openNextFile();

    while (file) {
      String path = file.path();
      if (path.startsWith("/littlefs")) {
        path.replace("/littlefs", "");
      }

      if (path.indexOf("bootrec_") >= 0 && path.endsWith(".log")) {
        bootFileCount++;
      }

      file.close();
      file = root.openNextFile();
      yield();
    }

    root.close();

    if (bootFileCount <= LOGGER_MAX_BOOT_FILES) return;

    String oldest = _findOldestBootFile();
    if (oldest.length() == 0) return;

    _clearFailure(oldest);

    if (LittleFS.remove(oldest)) {
      loggerStatus("LOGGER: boot-record files over cap; deleted oldest " + oldest);
    } else {
      loggerStatus("LOGGER ERR: failed deleting oldest boot-record file " + oldest);
      return;
    }
  }
}

String Logger::_fileNameForPath(const String& path) const {
  String name = path;
  int slash = name.lastIndexOf('/');

  if (slash >= 0) {
    name = name.substring(slash + 1);
  }

  struct tm timeinfo;
  char timestamp[32];

  if (getLocalTime(&timeinfo, 10)) {
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H-%M-%S", &timeinfo);
  } else {
    snprintf(timestamp, sizeof(timestamp), "ms_%lu", (unsigned long)millis());
  }

  return _deviceId + "_" + String(timestamp) + "_" + name;
}

String Logger::_urlEncode(const String& input) const {
  String output;
  const char* hex = "0123456789ABCDEF";

  // Reserve some space to reduce heap fragmentation.
  output.reserve(input.length() + (input.length() / 2));

  for (size_t i = 0; i < input.length(); i++) {
    uint8_t c = (uint8_t)input[i];

    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      output += (char)c;
    } else if (c == ' ') {
      output += "%20";
    } else {
      output += '%';
      output += hex[(c >> 4) & 0x0F];
      output += hex[c & 0x0F];
    }

    if ((i & 0xFFU) == 0) {
      yield();
    }
  }

  return output;
}

int Logger::_doGet(HTTPClient& http,
                   WiFiClientSecure& client,
                   const String& url) {
  if (!http.begin(client, url)) {
    return -1;
  }

  yield();
  return http.GET();
}

uint8_t Logger::_registerFailure(const String& path) {
  if (_failedPath == path) {
    if (_failedCount < 255) _failedCount++;
  } else {
    _failedPath = path;
    _failedCount = 1;
  }
  return _failedCount;
}

void Logger::_clearFailure(const String& path) {
  if (_failedPath == path) {
    _failedPath = "";
    _failedCount = 0;
  }
}

bool Logger::_uploadOneFile(const String& path) {
  if (!_fsReady ||
      !_enabled ||
      _uploadBusy ||
      WiFi.status() != WL_CONNECTED) {
    return false;
  }

  _uploadBusy = true;

  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    loggerStatus("LOGGER: queued file vanished; skipping " + path);
    _clearFailure(path);
    _uploadBusy = false;
    return false;
  }

  size_t fileSize = file.size();

  // A file that cannot be uploaded with the current GET architecture must not
  // poison the queue forever.
  if (fileSize == 0 || fileSize > LOGGER_MAX_SAFE_GET_BYTES) {
    file.close();
    _clearFailure(path);
    LittleFS.remove(path);

    loggerStatus("LOGGER: discarded unusable queued file " +
                 path + " (" + String(fileSize) + " bytes).");

    _uploadBusy = false;
    return false;
  }

  String content;
  if (!content.reserve(fileSize + 1)) {
    file.close();

    uint8_t failures = _registerFailure(path);

    loggerStatus("LOGGER: memory unavailable for " + path +
                 "; failure " + String(failures) + "/" +
                 String(LOGGER_MAX_FAILED_RETRIES));

    if (failures >= LOGGER_MAX_FAILED_RETRIES) {
      loggerStatus("LOGGER: memory failure; file retained for later retry " + path);
    }

    _uploadBusy = false;
    return false;
  }

  while (file.available()) {
    content += (char)file.read();

    if ((content.length() & 0xFFU) == 0) {
      yield();
    }
  }

  file.close();

  String encodedContent = _urlEncode(content);
  content = "";

  String url = _appsScriptUrl +
               "?deviceId=" + _urlEncode(_deviceId) +
               "&filename=" + _urlEncode(_fileNameForPath(path)) +
               "&content=" + encodedContent;

  encodedContent = "";

  if (_apiKey.length() > 0) {
    url += "&key=" + _urlEncode(_apiKey);
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(LOGGER_HTTP_TIMEOUT_MS / 1000U);

  HTTPClient http;
  http.setConnectTimeout(LOGGER_HTTP_TIMEOUT_MS);
  http.setTimeout(LOGGER_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  uint32_t startedMs = millis();
  int code = _doGet(http, client, url);
  uint32_t elapsedMs = millis() - startedMs;

  http.end();
  client.stop();

  bool success = code >= 200 && code < 300;

  loggerStatus(String(success ? "LOGGER: Upload Success " : "LOGGER: Upload Failed ") +
               path +
               " HTTP=" + String(code) +
               " bytes=" + String(fileSize) +
               " elapsed=" + String(elapsedMs) + "ms");

  if (success) {
    _clearFailure(path);
    LittleFS.remove(path);
  } else {
    uint8_t failures = _registerFailure(path);

    if (failures >= LOGGER_MAX_FAILED_RETRIES) {
      loggerStatus("LOGGER: repeated network failure; queued file retained " + path);
    } else {
      loggerStatus("LOGGER: file retained for one later retry; failure " +
                   String(failures) + "/" +
                   String(LOGGER_MAX_FAILED_RETRIES));
    }
  }

  _uploadBusy = false;
  return success;
}

bool Logger::_uploadNextQueuedFile() {
  if (!_fsReady || !_enabled || _uploadBusy) return false;

  // Boot-record files take priority over the ordinary queue: whenever there
  // is any connectivity at all, get the reset-reason record for the most
  // recent boot(s) out first, ahead of whatever backlog of ordinary
  // diagnostic chatter may have built up in the meantime.
  String oldestBoot = _findOldestBootFile();
  if (oldestBoot.length() > 0) {
    return _uploadOneFile(oldestBoot);
  }

  String oldest = _findOldestQueuedFile();

  if (oldest.length() == 0) {
    loggerStatus("LOGGER: no queued file ready for upload.");
    return false;
  }

  // Only one file is attempted during this loop cycle.
  return _uploadOneFile(oldest);
}

void Logger::forceUpload() {
  if (!_enabled || !_fsReady || _uploadBusy) return;

  _queueCurrentLog();
  _enforceQueueLimit();
  _uploadNextQueuedFile();
  _lastUploadAttemptMs = millis();
}

void Logger::loop() {
  uint32_t now = millis();

  if (!_enabled || !_fsReady) {
    static uint32_t lastDisabledPrintMs = 0;

    if ((uint32_t)(now - lastDisabledPrintMs) >= 60000UL) {
      lastDisabledPrintMs = now;
      Serial.printf("[LOGGER TIMER] disabled enabled=%d fsReady=%d\n",
                    _enabled ? 1 : 0,
                    _fsReady ? 1 : 0);
    }

    return;
  }

  static uint32_t lastTimerPrintMs = 0;

  if ((uint32_t)(now - lastTimerPrintMs) >= 60000UL) {
    lastTimerPrintMs = now;

    Serial.printf("[LOGGER TIMER] elapsed=%lu target=%lu wifi=%d busy=%d\n",
                  (unsigned long)(now - _lastUploadAttemptMs),
                  (unsigned long)_uploadEveryMs,
                  WiFi.status(),
                  _uploadBusy ? 1 : 0);
  }

  if (_uploadBusy || _uploadTaskActive) return;

  if ((uint32_t)(now - _lastUploadAttemptMs) < _uploadEveryMs) {
    return;
  }

  _lastUploadAttemptMs = now;

  loggerStatus("[LOGGER TIMER] Upload interval reached.");

  // Rotate the active file, trim queue to five, and attempt only one upload.
  _queueCurrentLog();
  _enforceQueueLimit();
  _enforceBootFileLimit(); // periodic safety net; logBootRecord() also self-enforces

  // Added 2026-08-05: WiFi.status() == WL_CONNECTED only means associated
  // to the local router -- it says nothing about whether the internet path
  // this upload actually needs works. On a WiFi-but-no-internet network,
  // this used to still attempt _uploadNextQueuedFile() every 30 minutes,
  // burning its own 8-second bounded timeout for nothing every time.
  // internetReachable is maintained by main.cpp's isolated, non-blocking
  // DNS-check task (built for the same underlying condition that caused
  // connectToFirebase()'s crash loop) -- reusing that signal here avoids
  // needing a second, redundant reachability check. Local rotation and
  // queue trimming above this line are unconditional and unaffected --
  // they don't touch the network and were never part of the problem.
  extern volatile bool internetReachable;

  if (WiFi.status() == WL_CONNECTED && internetReachable) {
    // The upload runs on its own task so a slow or dead connection can never
    // block (or watchdog-reset) the main loop that runs the pumps.
    _startUploadTask();
  } else if (WiFi.status() != WL_CONNECTED) {
    loggerStatus("LOGGER: WiFi unavailable; queued files retained.");
  } else {
    loggerStatus("LOGGER: internet unreachable (DNS); queued files retained.");
  }

  _enforceQueueLimit();
}

// ---------------------------------------------------------------------------
// Upload on a separate task. loopTask returns immediately.
// ---------------------------------------------------------------------------
void Logger::_uploadTaskEntry(void* arg) {
  Logger* self = static_cast<Logger*>(arg);

  traceStage(1, "log-upload");
  self->_uploadNextQueuedFile();
  traceStage(1, "idle");

  // BearSSL/mbedTLS cleanup can continue briefly after the client closes.
  unsigned long until = millis() + 5000UL;
  if ((long)(until - apexTlsCooldownUntilMs) > 0) apexTlsCooldownUntilMs = until;
  loggerTlsActive = false;
  self->_uploadTaskActive = false;
  vTaskDelete(nullptr);
}

void Logger::_startUploadTask() {
  if (_uploadTaskActive || _uploadBusy) return;

  // A TLS session needs tens of KB of contiguous heap. Skip this slot rather
  // than risk starving the rest of the firmware.
  if (ESP.getMaxAllocHeap() < 75000 || ESP.getFreeHeap() < 110000) {
    loggerStatus("LOGGER: insufficient TLS headroom; upload deferred (free=" +
                 String((unsigned)ESP.getFreeHeap()) + ", maxAlloc=" +
                 String((unsigned)ESP.getMaxAllocHeap()) + ")");
    return;
  }

  _uploadTaskActive = true;
  loggerTlsActive = true;
  BaseType_t ok = xTaskCreatePinnedToCore(_uploadTaskEntry, "logUpload", 12288, this, 1, nullptr, 0);
  if (ok != pdPASS) {
    loggerTlsActive = false;
    _uploadTaskActive = false;
    loggerStatus("LOGGER: could not start upload task; skipped this slot.");
  }
}
