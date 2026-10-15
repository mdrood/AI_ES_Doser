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

  // Writes one line of the current boot's one-time banner (reset reason,
  // firmware version, etc.) to a small dedicated file that is exempt from
  // the normal 5-file queue eviction and is uploaded ahead of everything
  // else. Ordinary logger.println()/printf() output shares a queue that is
  // capped at LOGGER_MAX_QUEUED_FILES and drained at only one file per
  // upload cycle -- since the boot banner is always the oldest thing in
  // that queue right after a reboot, it was reliably the first thing
  // evicted before it could ever be uploaded. This path fixes that without
  // touching the normal queue's behavior at all.
  void logBootRecord(const String& line);

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

  // Path of this boot's dedicated boot-record file. Empty until the first
  // logBootRecord() call of this boot, then reused for the rest of it.
  String _bootRecordPath;

  String _currentPath() const;
  String _queuePath(uint32_t stamp) const;
  String _nextQueuePath() const;
  String _nextBootRecordPath() const;
  String _timestampPrefix() const;

  void _writeRaw(const String& s, bool addNewline);
  void _rotateIfNeeded();
  void _queueCurrentLog();
  void _enforceQueueLimit();
  void _enforceBootFileLimit();
  String _findOldestQueuedFile();
  String _findOldestBootFile();
  bool _uploadOneFile(const String& path);
  bool _uploadNextQueuedFile();
  String _fileNameForPath(const String& path) const;
  String _urlEncode(const String& in) const;
  int _doGet(HTTPClient& http, WiFiClientSecure& client, const String& url);
  uint8_t _registerFailure(const String& path);
  void _clearFailure(const String& path);
};

extern Logger logger;