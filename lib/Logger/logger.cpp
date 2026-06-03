#include "logger.h"
#include <time.h>
#include <stdarg.h>

Logger logger;

// Force WebSerial/Serial logs to upload as small Google Drive files.
// This prevents long delayed logs from failing and keeps the exact WebSerial text.
//static const uint32_t LOGGER_FORCED_UPLOAD_EVERY_MS = 60000UL; // 1 minute
static const uint32_t LOGGER_FORCED_UPLOAD_EVERY_MS = 1800000UL; // 30 minutes
static const size_t LOGGER_MAX_SAFE_GET_BYTES = 24576;
static const uint8_t LOGGER_MAX_FAILED_RETRIES = 5;
static const char* LOGGER_CLEANUP_MARKER = "/logs/cleanup_done_30min_v1.flag";

static String loggerFailCountPath(const String& logPath) {
  String p = logPath;
  p.replace("/", "_");
  return "/logs/fail" + p + ".cnt";
}

static uint8_t loggerReadFailCount(const String& logPath) {
  String countPath = loggerFailCountPath(logPath);
  File f = LittleFS.open(countPath, FILE_READ);
  if (!f) return 0;
  String v = f.readString();
  f.close();
  return (uint8_t)constrain(v.toInt(), 0, 255);
}

static void loggerWriteFailCount(const String& logPath, uint8_t count) {
  String countPath = loggerFailCountPath(logPath);
  File f = LittleFS.open(countPath, FILE_WRITE);
  if (!f) return;
  f.print(String(count));
  f.close();
}

static void loggerClearFailCount(const String& logPath) {
  String countPath = loggerFailCountPath(logPath);
  if (LittleFS.exists(countPath)) LittleFS.remove(countPath);
}

static void loggerOneTimeLittleFSCleanup() {
  if (LittleFS.exists(LOGGER_CLEANUP_MARKER)) return;

  if (!LittleFS.exists("/logs")) {
    LittleFS.mkdir("/logs");
  }

  uint16_t removedLogs = 0;
  uint16_t removedCounts = 0;

  File root = LittleFS.open("/logs");
  if (root && root.isDirectory()) {
    File file = root.openNextFile();
    while (file) {
      String path = file.path();
      if (path.startsWith("/littlefs")) path.replace("/littlefs", "");
      file.close();

      bool removeQueuedLog = (path.indexOf("queued_") >= 0 && path.endsWith(".log"));
      bool removeRetryCount = (path.indexOf("fail_logs_queued_") >= 0 && path.endsWith(".cnt"));

      if (removeQueuedLog && LittleFS.remove(path)) removedLogs++;
      else if (removeRetryCount && LittleFS.remove(path)) removedCounts++;

      file = root.openNextFile();
      yield();
    }
  }

  File marker = LittleFS.open(LOGGER_CLEANUP_MARKER, FILE_WRITE);
  if (marker) {
    marker.print("done");
    marker.close();
  }

  Serial.printf("LOGGER: LittleFS one-time cleanup removed %u queued logs and %u retry counters.\n", removedLogs, removedCounts);
  WebSerial.printf("LOGGER: LittleFS one-time cleanup removed %u queued logs and %u retry counters.\n", removedLogs, removedCounts);
}

Logger::Logger()
  : _uploadEveryMs(LOGGER_UPLOAD_EVERY_MS),
    _rotateBytes(LOGGER_ROTATE_BYTES),
    _lastUploadAttemptMs(0),
    _enabled(false),
    _fsReady(false) {}

bool Logger::begin(const String& deviceId, const String& appsScriptUrl) {
  return begin(deviceId, appsScriptUrl, "", LOGGER_UPLOAD_EVERY_MS, LOGGER_ROTATE_BYTES);
}

bool Logger::begin(const String& deviceId,
                   const String& appsScriptUrl,
                   const String& apiKey,
                   uint32_t uploadEveryMs,
                   size_t rotateBytes) {
  _deviceId = deviceId;
  _appsScriptUrl = appsScriptUrl;
  _apiKey = apiKey;

  // User-proven fix: shorter chunks upload reliably.
  // 30 minutes has tested working; even if main.cpp passes 2 hours, use this forced interval.
  _uploadEveryMs = LOGGER_FORCED_UPLOAD_EVERY_MS;

  // Keep caller-supplied rotate size, but upload is now time-based at the forced interval.
  _rotateBytes = rotateBytes;
  _enabled = true;

  if (!LittleFS.begin(true)) {
    Serial.println("LOGGER: LittleFS mount failed.");
    _fsReady = false;
    return false;
  }

  _fsReady = true;
  _lastUploadAttemptMs = millis();
  
  if (!LittleFS.exists("/logs")) {
    LittleFS.mkdir("/logs");
  }

  // Remote-device recovery: remove old stuck queued logs once after OTA.
  loggerOneTimeLittleFSCleanup();

  println("LOGGER: started local-first Drive logger");
  printf("LOGGER: upload interval = %lu ms\n", (unsigned long)_uploadEveryMs);
  return true;
}

void Logger::setEnabled(bool enabled) { _enabled = enabled; }
bool Logger::isEnabled() const { return _enabled && _fsReady; }

void Logger::setUploadIntervalMs(uint32_t uploadEveryMs) {
  // Keep this logger reliable: force the tested Drive/WebSerial log interval.
  // Parameter is accepted for compatibility, but intentionally ignored.
  (void)uploadEveryMs;
  _uploadEveryMs = LOGGER_FORCED_UPLOAD_EVERY_MS;
}
uint32_t Logger::getUploadIntervalMs() const { return _uploadEveryMs; }

String Logger::_currentPath() const { return "/logs/current.log"; }
String Logger::_queuePath(uint32_t stamp) const { return "/logs/queued_" + String(stamp) + ".log"; }

String Logger::_nextQueuePath() const {
  static uint32_t counter = 0;
  return "/logs/queued_" + String((uint32_t)(millis() / 1000UL)) +
         "_" + String((uint32_t)(millis() & 0xFFFFUL)) +
         "_" + String(counter++) + ".log";
}

String Logger::_timestampPrefix() const {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 10)) {
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String("[") + buf + "] ";
  }
  return String("[ms=") + String(millis()) + "] ";
}

void Logger::print(const String& msg) { _writeRaw(msg, false); }
void Logger::print(const char* msg) { _writeRaw(String(msg ? msg : ""), false); }
void Logger::println(const String& msg) { _writeRaw(msg, true); }
void Logger::println(const char* msg) { _writeRaw(String(msg ? msg : ""), true); }
void Logger::println() { _writeRaw("", true); }

void Logger::printf(const char* fmt, ...) {
  if (!fmt) return;
  char stackBuf[256];
  va_list args;
  va_start(args, fmt);
  int needed = vsnprintf(stackBuf, sizeof(stackBuf), fmt, args);
  va_end(args);
  if (needed < 0) return;
  if ((size_t)needed < sizeof(stackBuf)) {
    _writeRaw(String(stackBuf), false);
    return;
  }
  char* heapBuf = (char*)malloc((size_t)needed + 1);
  if (!heapBuf) return;
  va_start(args, fmt);
  vsnprintf(heapBuf, (size_t)needed + 1, fmt, args);
  va_end(args);
  _writeRaw(String(heapBuf), false);
  free(heapBuf);
}

void Logger::log(const String& msg) { println(msg); }
void Logger::log(const char* msg) { println(msg); }

void Logger::_writeRaw(const String& s, bool addNewline) {
  if (addNewline) {
    Serial.println(s);
    WebSerial.println(s);
  } else {
    Serial.print(s);
    WebSerial.print(s);
  }

  if (!_enabled || !_fsReady) return;

  File f = LittleFS.open(_currentPath(), FILE_APPEND);
  if (!f) {
      if (!LittleFS.exists("/logs")) LittleFS.mkdir("/logs");
      f = LittleFS.open(_currentPath(), FILE_WRITE);
      
      if (!f) {
          WebSerial.println("LOGGER ERR: Disk Write Failed!"); 
          return; 
      }
  }

  if (f) {
      if (addNewline) {
        f.print(_timestampPrefix());
        f.println(s);
      } else {
        f.print(s);
      }
      f.close();
  }

  _rotateIfNeeded();
}

void Logger::_rotateIfNeeded() {
  if (!_fsReady) return;
  File f = LittleFS.open(_currentPath(), FILE_READ);
  if (!f) return;
  size_t sz = f.size();
  f.close();

  if (sz < _rotateBytes) return;

  String next = _nextQueuePath();
  if (!LittleFS.rename(_currentPath(), next)) {
    Serial.println("LOGGER ERR: Rotate rename failed.");
    WebSerial.println("LOGGER ERR: Rotate rename failed.");
  } else {
    Serial.print("LOGGER: rotated current log to ");
    Serial.println(next);
    WebSerial.print("LOGGER: rotated current log to ");
    WebSerial.println(next);
  }
}

String Logger::_fileNameForPath(const String& path) const {
  String p = path;
  int slash = p.lastIndexOf('/');
  if (slash >= 0) p = p.substring(slash + 1);

  struct tm timeinfo;
  char ts[32];
  if (getLocalTime(&timeinfo, 10)) {
    strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &timeinfo);
  } else {
    snprintf(ts, sizeof(ts), "ms_%lu", (unsigned long)millis());
  }
  return _deviceId + "_" + String(ts) + "_" + p;
}

String Logger::_urlEncode(const String& in) const {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else if (c == ' ') out += "%20";
    else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

int Logger::_doGet(HTTPClient &http, WiFiClientSecure &client, const String &url) {
    if (!http.begin(client, url)) return -1;
    yield();
    return http.GET(); 
}

bool Logger::_uploadOneFile(const String& path) {
  if (!_fsReady || !_enabled || WiFi.status() != WL_CONNECTED) return false;

  File f = LittleFS.open(path, FILE_READ);
  if (!f) return false;
  
  // Safety check: do NOT delete logs just because they are too large.
  // Leave the file queued so the log is not lost.
  size_t fileSize = f.size();
  if (fileSize > LOGGER_MAX_SAFE_GET_BYTES) { 
    f.close();
    String keepMsg = "LOGGER: Keeping oversized queued log " + path + " (" + String(fileSize) + " bytes); not uploading with GET.";
    Serial.println(keepMsg);
    WebSerial.println(keepMsg);
    return false;
  }

  String content = f.readString();
  f.close();

  String url = _appsScriptUrl + "?deviceId=" + _urlEncode(_deviceId) + 
               "&filename=" + _urlEncode(_fileNameForPath(path)) +
               "&content=" + _urlEncode(content);
  
  if (_apiKey.length() > 0) url += "&key=" + _urlEncode(_apiKey);

  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient http;
  http.setTimeout(15000); 
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); 

  int code = _doGet(http, client, url);
  String response = http.getString();
  http.end();

  bool ok = (code >= 200 && code < 300);
  
  String statusMsg = ok ? "LOGGER: Upload Success " : "LOGGER: Upload Failed ";
  statusMsg += path + " (HTTP " + String(code) + ", bytes " + String(fileSize) + ")";
  
  Serial.println(statusMsg);
  WebSerial.println(statusMsg);

  if (!ok && response.length() > 0) {
    // Print only a small preview so the error itself does not bloat the next log.
    String preview = response.substring(0, 240);
    Serial.print("LOGGER: Upload response: ");
    Serial.println(preview);
    WebSerial.print("LOGGER: Upload response: ");
    WebSerial.println(preview);
  }

  if (ok) {
    loggerClearFailCount(path);
    LittleFS.remove(path);
  } else {
    uint8_t fails = loggerReadFailCount(path);
    fails++;
    loggerWriteFailCount(path, fails);

    if (fails >= LOGGER_MAX_FAILED_RETRIES) {
      String discardMsg = "LOGGER: Deleting stuck queued log " + path +
                          " after " + String(fails) + " failed uploads.";
      Serial.println(discardMsg);
      WebSerial.println(discardMsg);
      loggerClearFailCount(path);
      LittleFS.remove(path);
    } else {
      String retryMsg = "LOGGER: Keeping queued log for retry (fail " +
                        String(fails) + "/" + String(LOGGER_MAX_FAILED_RETRIES) + ").";
      Serial.println(retryMsg);
      WebSerial.println(retryMsg);
    }
  }
  
  return ok;
}

void Logger::_uploadQueuedFiles() {
  if (!_fsReady || !_enabled) return;

  File cur = LittleFS.open(_currentPath(), FILE_READ);
  if (cur) {
    size_t sz = cur.size();
    cur.close();
    if (sz > 0) {
        String queuedPath = _nextQueuePath();
        if (!LittleFS.rename(_currentPath(), queuedPath)) {
          Serial.println("LOGGER ERR: Queue rename failed.");
          WebSerial.println("LOGGER ERR: Queue rename failed.");
        } else {
          Serial.print("LOGGER: queued current log as ");
          Serial.println(queuedPath);
          WebSerial.print("LOGGER: queued current log as ");
          WebSerial.println(queuedPath);
        }
    }
  }

  File root = LittleFS.open("/logs");
  if (!root || !root.isDirectory()) return;

  File file = root.openNextFile();
  while (file) {
    String path = file.path();
    if (path.startsWith("/littlefs")) path.replace("/littlefs", "");
    
    bool isQueued = (path.indexOf("queued_") >= 0 && path.endsWith(".log"));
    file.close(); 

    if (isQueued) {
      _uploadOneFile(path);
      for (int i = 0; i < 2; i++) {
          yield();
          delay(50); 
      }
    }
    file = root.openNextFile();
    yield();
  }
}

void Logger::forceUpload() { 
  _uploadQueuedFiles(); 
  _lastUploadAttemptMs = millis(); 
}

void Logger::loop() {
  uint32_t now = millis();

  if (!_enabled || !_fsReady) {
    static uint32_t lastDisabledPrint = 0;
    if ((uint32_t)(now - lastDisabledPrint) >= 60000UL) {
      lastDisabledPrint = now;
      Serial.printf("[LOGGER TIMER] disabled enabled=%d fsReady=%d\n", _enabled ? 1 : 0, _fsReady ? 1 : 0);
      WebSerial.printf("[LOGGER TIMER] disabled enabled=%d fsReady=%d\n", _enabled ? 1 : 0, _fsReady ? 1 : 0);
    }
    return;
  }

  static uint32_t lastTimerPrint = 0;
  if ((uint32_t)(now - lastTimerPrint) >= 60000UL) {
    lastTimerPrint = now;
    Serial.printf("[LOGGER TIMER] elapsed=%lu target=%lu wifi=%d\n",
                  (unsigned long)(now - _lastUploadAttemptMs),
                  (unsigned long)_uploadEveryMs,
                  WiFi.status());
    WebSerial.printf("[LOGGER TIMER] elapsed=%lu target=%lu wifi=%d\n",
                     (unsigned long)(now - _lastUploadAttemptMs),
                     (unsigned long)_uploadEveryMs,
                     WiFi.status());
  }

  if ((uint32_t)(now - _lastUploadAttemptMs) >= _uploadEveryMs) {
    _lastUploadAttemptMs = now;
    Serial.println("[LOGGER TIMER] Upload interval reached.");
    WebSerial.println("[LOGGER TIMER] Upload interval reached.");
    _uploadQueuedFiles();
  }
}