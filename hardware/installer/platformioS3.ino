[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

board_build.flash_size = 8MB
board_upload.flash_size = 8MB
board_build.partitions = partitions_8M.csv
board_build.arduino.memory_type = qio_opi

monitor_speed = 115200
upload_speed = 115200

build_flags =
    -D BOARD_HAS_PSRAM
    -D MB_FS_LITTLEFS_ESP32
    -D USER_CONFIG_LITTLEFS_FS

lib_deps =
    bblanchon/ArduinoJson @ ^7.4.3
    mobizt/Firebase ESP32 Client @ ^4.4.17
    ayushsharma82/WebSerial@^2.1.2
    ESP32Async/AsyncTCP@^3.4.10
    ESP32Async/ESPAsyncWebServer@^3.10.3