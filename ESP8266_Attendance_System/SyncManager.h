#ifndef SYNC_MANAGER_H
#define SYNC_MANAGER_H

#include <Arduino.h>
#include "Config.h"
#include "StorageDriver.h"
#include "RTC_Driver.h"
#include "AttendanceLogic.h"
#include "NetworkManager.h"

// Tier 6: everything that talks to the outside world beyond raw WiFi —
// posting to the Google Apps Script webhook, draining the offline
// queue in bounded batches, and periodically correcting RTC drift
// via NTP. Decoupled from AttendanceLogic: it only ever receives a
// finished AttendanceEvent, never reaches into HAL/RFID directly.
//
// NOTE on blocking: the actual HTTPS POST call blocks the loop for up
// to SYNC_HTTP_TIMEOUT_MS while it's in flight. This is a deliberate,
// bounded tradeoff (not a busy-wait) — it only happens when a scan
// needs sending or a queued batch is being flushed, which is
// infrequent relative to the RFID poll rate.
class SyncManager {
public:
    SyncManager(StorageDriver &storage, RTC_Driver &rtc, NetworkManager &net);

    void begin();
    void tick(); // non-blocking gate; drives auto-flush + periodic NTP resync

    // Call right after AttendanceLogic produces a VALID_SCAN event.
    // Tries a live send if online; queues on failure/offline.
    void sendOrQueue(const AttendanceEvent &event);

    // Wire this to the button's SHORT_PRESS event.
    void requestManualSync();

    int pendingCount();

private:
    StorageDriver &_storage;
    RTC_Driver &_rtc;
    NetworkManager &_net;

    bool _wasConnected = false;
    bool _flushRequested = false;
    uint32_t _lastFlushAttemptMs = 0;
    uint32_t _lastNtpSyncMs = 0;

    bool _postJson(const String &jsonBody);
    bool _sendSingleRecord(const String &uid, const StaffInfo &staff, AttendanceType type, uint32_t ts);
    void _flushQueueBatch();
    void _syncTimeFromNtp();
};

#endif // SYNC_MANAGER_H
