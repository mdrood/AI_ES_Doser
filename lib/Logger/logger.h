#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WebSerial.h>

#ifndef LOGGER_UPLOAD_EVERY_MS
#define LOGGER_UPLOAD_EVERY_MS (30UL * 60UL * 1000UL) // 30 minutes
#endif

#ifndef LOGGER_ROTATE_BYTES
#define LOGGER_ROTATE_BYTES (4UL * 1024UL) // GET uploader must stay small
#endif

class Logger {
public:
  Logger();

  bool begin(const String& deviceId, const String& appsScriptUrl);
  bool begin(const String& deviceId,
             const String& appsScriptUrl,
             const String& apiKey,
             uint32_t uploadEveryMs = LOGGER_UPLOAD_EVERY_MS,
             size_t rotateBytes = LOGGER_ROTATE_BYTES);

  void loop();
  void setUploadIntervalMs(uint32_t uploadEveryMs);
  uint32_t getUploadIntervalMs() const;

  void print(const String& msg);
  void print(const char* msg);
  void println(const String& msg);
  void println(const char* msg);
  void println();
  void printf(const char* fmt, ...);

  void log(const String& msg);
  void log(const char* msg);

  void forceUpload();
  void setEnabled(bool enabled);
  bool isEnabled() const;

private:
  String _deviceId;
  String _appsScriptUrl;
  String _apiKey;
  uint32_t _uploadEveryMs;
  size_t _rotateBytes;
  uint32_t _lastUploadAttemptMs;
  bool _enabled;
  bool _fsReady;
  bool _uploadBusy;

  // Retry tracking stays in RAM; no long LittleFS sidecar filenames.
  String _failedPath;
  uint8_t _failedCount;

  String _currentPath() const;
  String _queuePath(uint32_t stamp) const;
  String _nextQueuePath() const;
  String _timestampPrefix() const;

  void _writeRaw(const String& s, bool addNewline);
  void _rotateIfNeeded();
  void _queueCurrentLog();
  void _enforceQueueLimit();
  String _findOldestQueuedFile();
  bool _uploadOneFile(const String& path);
  bool _uploadNextQueuedFile();
  String _fileNameForPath(const String& path) const;
  String _urlEncode(const String& in) const;
  int _doGet(HTTPClient& http, WiFiClientSecure& client, const String& url);
  uint8_t _registerFailure(const String& path);
  void _clearFailure(const String& path);
};

extern Logger logger;