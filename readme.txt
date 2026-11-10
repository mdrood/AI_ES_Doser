reefDoserT BENCH PATCH (not production certified)

Replace src/main.cpp and lib/Logger/logger.cpp in the original PlatformIO project. All other files remain unchanged.

Changes: suppress forced restart on DNS/token outages; gate Firebase startup/refresh on recent DNS success and TLS headroom; defer Google Drive TLS uploads unless contiguous/free heap thresholds are met; retain queued files after upload failures.

Limitations: the logger still has a five-file queue cap and can evict older queued files as new logs arrive. TLS calls may still block and watchdog reset. Firebase SDK retry logic is unchanged. No compilation or hardware test performed. Verify pump safety and OTA before any production use.
