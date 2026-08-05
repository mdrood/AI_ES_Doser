#include "logger.h"
#include <time.h>
#include <stdarg.h>

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
static const uint8_t LOGGER_MAX_FAILED_RETRIES = 2;
static const uint16_t LOGGER_HTTP_TIMEOUT_MS = 8000;

static void loggerStatus(const String& message) {
  Serial.println(message);
  WebSerial.println(message);
}

Logger::Logger()
  : _uploadEveryMs(LOGGER_UPLOAD_EVERY_MS),
    _rotateBytes(LOGGER_ROTATE_BYTES),
    _lastUploadAttemptMs(0),
    _enabled(false),
    _fsReady(false),
    _uploadBusy(false),
    _failedPath(""),
    _failedCount(0) {}

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
  _deviceId = deviceId;
  _appsScriptUrl = appsScriptUrl;
  _apiKey = apiKey;

  // Keep the tested 30-minute production interval.
  (void)uploadEveryMs;
  _uploadEveryMs = LOGGER_FORCED_UPLOAD_EVERY_MS;

  // The current Apps Script receives the full log in a GET URL.
  // Force 4 KB files so URL encoding stays within a safe range.
  (void)rotateBytes;
  _rotateBytes = LOGGER_MAX_SAFE_GET_BYTES;

  _enabled = true;
  _uploadBusy = false;
  _failedPath = "";
  _failedCount = 0;

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

  // Do not erase the queue at boot. Just trim it safely to the newest five.
  _enforceQueueLimit();

  println("LOGGER: started fail-safe local-first Drive logger");
  printf("LOGGER: interval=%lu ms rotate=%u bytes maxQueue=%u timeout=%u ms retries=%u\n",
         (unsigned long)_uploadEveryMs,
         (unsigned)_rotateBytes,
         (unsigned)LOGGER_MAX_QUEUED_FILES,
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

String Logger::_nextQueuePath() const {
  static uint32_t counter = 0;

  return "/logs/queued_" +
         String((uint32_t)(millis() / 1000UL)) + "_" +
         String((uint32_t)(millis() & 0xFFFFUL)) + "_" +
         String(counter++) + ".log";
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

void Logger::_writeRaw(const String& text, bool addNewline) {
  if (addNewline) {
    Serial.println(text);
    WebSerial.println(text);
  } else {
    Serial.print(text);
    WebSerial.print(text);
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
      _clearFailure(path);
      LittleFS.remove(path);
      loggerStatus("LOGGER: dropped file after repeated memory failures " + path);
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
      _clearFailure(path);
      LittleFS.remove(path);

      loggerStatus("LOGGER: abandoned stuck file after " +
                   String(failures) + " failures: " + path);
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

  if (_uploadBusy) return;

  if ((uint32_t)(now - _lastUploadAttemptMs) < _uploadEveryMs) {
    return;
  }

  _lastUploadAttemptMs = now;

  loggerStatus("[LOGGER TIMER] Upload interval reached.");

  // Rotate the active file, trim queue to five, and attempt only one upload.
  _queueCurrentLog();
  _enforceQueueLimit();

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
    _uploadNextQueuedFile();
  } else if (WiFi.status() != WL_CONNECTED) {
    loggerStatus("LOGGER: WiFi unavailable; queued files retained.");
  } else {
    loggerStatus("LOGGER: internet unreachable (DNS); queued files retained.");
  }

  _enforceQueueLimit();
}