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
// NOTE on blocking: _postJson blocks the loop for up to SYNC_HTTP_TIMEOUT_MS
// while a request is in flight. This only happens inside tick() (called at
// the top of loop()), so it NEVER holds up the LCD transient display or RFID
// scanning. Scan events are enqueued instantly via enqueue() and flushed by
// tick() on the very next loop iteration.
class SyncManager {
public:
    SyncManager(StorageDriver &storage, RTC_Driver &rtc, NetworkManager &net);

    void begin();
    void tick(); // non-blocking gate; drives auto-flush + periodic NTP resync

    // PRIMARY path: write the event to the offline queue immediately (non-
    // blocking flash write, <1 ms) and request an immediate flush so tick()
    // sends it on the very next loop iteration. Use this from the main loop
    // so the HTTPS POST never blocks the LCD transient or RFID scanning.
    void enqueue(const AttendanceEvent &event);

    // LEGACY: tries a live HTTPS send first, falls back to queuing.
    // Blocks the loop for up to SYNC_HTTP_TIMEOUT_MS — avoid calling this
    // from inside the scan handler; prefer enqueue() instead.
    void sendOrQueue(const AttendanceEvent &event);

    // Wire this to the button's SHORT_PRESS event.
    void requestManualSync();
    void syncDailyStateFromCloud();

    int pendingCount();

private:
    StorageDriver &_storage;
    RTC_Driver &_rtc;
    NetworkManager &_net;

    bool _wasConnected = false;
    bool _flushRequested = false;
    uint32_t _lastFlushAttemptMs = 0;
    uint32_t _lastNtpSyncMs = 0;
    bool _ntpRetryNeeded = false;
    uint32_t _ntpRetryAtMs = 0;

    bool _ntpSyncPending = false;
    uint32_t _ntpStartMs = 0;
    bool _cloudStateSyncPending = false;

    uint32_t _nextFlushAllowedMs = 0;
    uint32_t _flushBackoffMs = 5000;

    bool _postJson(const String &jsonBody);
    bool _sendSingleRecord(const String &uid, const StaffInfo &staff, AttendanceType type, uint32_t ts);
    void _flushQueueBatch();
    void _startNtpSync();
};

#endif // SYNC_MANAGER_H
