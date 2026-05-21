#include "logger.h"
#include <time.h>
#include <stdarg.h>

Logger logger;

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
  _uploadEveryMs = uploadEveryMs;
  _rotateBytes = rotateBytes;
  _enabled = true;

  if (!LittleFS.begin(true)) {
    Serial.println("LOGGER: LittleFS mount failed.");
    _fsReady = false;
    return false;
  }

  _fsReady = true;
  _lastUploadAttemptMs = millis() - _uploadEveryMs + 60000UL;
  
  if (!LittleFS.exists("/logs")) {
    LittleFS.mkdir("/logs");
  }

  println("LOGGER: started local-first Drive logger");
  return true;
}

void Logger::setEnabled(bool enabled) { _enabled = enabled; }
bool Logger::isEnabled() const { return _enabled && _fsReady; }

void Logger::setUploadIntervalMs(uint32_t uploadEveryMs) { _uploadEveryMs = uploadEveryMs; }
uint32_t Logger::getUploadIntervalMs() const { return _uploadEveryMs; }

String Logger::_currentPath() const { return "/logs/current.log"; }
String Logger::_queuePath(uint32_t stamp) const { return "/logs/queued_" + String(stamp) + ".log"; }

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
  // 1. IMMEDIATE OUTPUT: Send to WebSerial/Serial before storage attempts
  if (addNewline) {
    Serial.println(s);
    WebSerial.println(s);
  } else {
    Serial.print(s);
    WebSerial.print(s);
  }

  if (!_enabled || !_fsReady) return;

  // 2. STORAGE ATTEMPT
  File f = LittleFS.open(_currentPath(), FILE_APPEND);
  if (!f) {
      if (!LittleFS.exists("/logs")) LittleFS.mkdir("/logs");
      f = LittleFS.open(_currentPath(), FILE_WRITE);
      
      if (!f) {
          // If storage is totally locked, we notify the user via WebSerial
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

  String next = _queuePath((uint32_t)time(nullptr));
  LittleFS.rename(_currentPath(), next);
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
  String content = f.readString();
  f.close();

  String url = _appsScriptUrl + "?deviceId=" + _urlEncode(_deviceId) + 
               "&filename=" + _urlEncode(_fileNameForPath(path)) +
               "&content=" + _urlEncode(content);
  
  if (_apiKey.length() > 0) url += "&key=" + _urlEncode(_apiKey);

  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient http;
  http.setTimeout(30000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); 

  int code = _doGet(http, client, url);
  http.end();

  bool ok = (code >= 200 && code < 300);
  
  // ALWAYS notify WebSerial of the outcome
  String statusMsg = ok ? "LOGGER: Upload Success " : "LOGGER: Upload Failed ";
  statusMsg += path + " (HTTP " + String(code) + ")";
  
  Serial.println(statusMsg);
  WebSerial.println(statusMsg);

  if (ok) {
    LittleFS.remove(path);
  } else if (code == 400 || code == 413 || code == 414) {
    // Permanent upload failure: malformed request or URL too large.
    // Do not retry this same bad queued log forever. Temporary failures
    // like -2, 408, 429, or 5xx are left in LittleFS for later retry.
    String discardMsg = "LOGGER: Discarding bad queued log " + path +
                        " after permanent HTTP " + String(code);
    Serial.println(discardMsg);
    WebSerial.println(discardMsg);
    LittleFS.remove(path);
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
        LittleFS.rename(_currentPath(), _queuePath((uint32_t)time(nullptr)));
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
      
      for(int i = 0; i < 5; i++) {
          yield();
          delay(100); 
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
  if (!_enabled || !_fsReady) return;

  uint32_t now = millis();

  static uint32_t lastTimerPrint = 0;
  if ((uint32_t)(now - lastTimerPrint) >= 60000UL) {
    lastTimerPrint = now;
    Serial.printf("[LOGGER TIMER] elapsed=%lu target=%lu\n",
                  (unsigned long)(now - _lastUploadAttemptMs),
                  (unsigned long)_uploadEveryMs);
  }

  if ((uint32_t)(now - _lastUploadAttemptMs) >= _uploadEveryMs) {
    _lastUploadAttemptMs = now;
    Serial.println("[LOGGER TIMER] Upload interval reached.");
    _uploadQueuedFiles();
  }
}