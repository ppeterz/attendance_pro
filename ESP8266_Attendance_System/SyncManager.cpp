#include "SyncManager.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

SyncManager::SyncManager(StorageDriver &storage, RTC_Driver &rtc, NetworkManager &net)
    : _storage(storage), _rtc(rtc), _net(net) {}

void SyncManager::begin() {
    _wasConnected = false;
    _lastFlushAttemptMs = 0;
    _lastNtpSyncMs = 0;
}

int SyncManager::pendingCount() {
    return _storage.queueCount();
}

void SyncManager::requestManualSync() {
    _flushRequested = true;
}

// ---------------- low-level HTTPS POST ----------------

bool SyncManager::_postJson(const String &jsonBody) {
    WiFiClientSecure client;
    // Pragmatic choice for a field device: skip certificate validation
    // rather than pin Google's rotating cert fingerprint (which would
    // silently break sync every time Google rotates it). Traffic still
    // travels over TLS; this only forgoes verifying the cert chain.
    client.setInsecure();

    HTTPClient https;
    https.setTimeout(SYNC_HTTP_TIMEOUT_MS);

    if (!https.begin(client, SYNC_ENDPOINT_URL)) {
        return false;
    }
    https.addHeader("Content-Type", "application/json");

    int httpCode = https.POST(jsonBody);
    bool ok = false;
    if (httpCode == 200) {
        // Apps Script ALWAYS returns HTTP 200, even for a rejected
        // request (bad secret, malformed body, etc.) — there's no way
        // for it to send a real error status code. So httpCode==200
        // only means "the script ran", not "it accepted the data".
        // We have to check the JSON body's status field to know if the
        // write actually happened; otherwise a rejected batch would
        // look like a success and get wrongly cleared from the queue.
        String resp = https.getString();
        ok = resp.indexOf("\"status\":\"ok\"") >= 0;
    }
    https.end();
    return ok;
}

bool SyncManager::_sendSingleRecord(const String &uid, const StaffInfo &staff,
                                     AttendanceType type, uint32_t ts) {
    // Built via ArduinoJson (not string concatenation) so a name/role
    // typed with a quote/backslash during enrollment can't break the
    // JSON payload.
    StaticJsonDocument<SYNC_RECORD_JSON_CAPACITY> doc;
    doc["secret"] = SYNC_SHARED_SECRET;
    doc["device"] = DEVICE_ID;
    JsonArray records = doc.createNestedArray("records");
    JsonObject rec = records.createNestedObject();
    rec["uid"] = uid;
    rec["fn"] = staff.firstName;
    rec["ln"] = staff.lastName;
    rec["role"] = staff.role;
    rec["sid"] = staff.staffId;
    rec["type"] = (type == AttendanceType::CHECK_IN) ? "IN" : "OUT";
    rec["ts"] = ts;

    String payload;
    serializeJson(doc, payload);
    return _postJson(payload);
}

// ---------------- public entry points ----------------

void SyncManager::sendOrQueue(const AttendanceEvent &event) {
    bool sent = false;
    if (_net.isConnected()) {
        sent = _sendSingleRecord(event.uid, event.staff, event.type, event.timestamp);
    }
    if (!sent) {
        _storage.appendToQueue(event.uid, event.staff, event.type, event.timestamp);
    }
}

void SyncManager::_flushQueueBatch() {
    int n = 0;
    String batch = _storage.readQueueBatchAsJsonArray(SYNC_MAX_BATCH_SIZE, n);
    if (n == 0) return;

    String payload = "{\"secret\":\"" + String(SYNC_SHARED_SECRET) +
                      "\",\"device\":\"" + String(DEVICE_ID) +
                      "\",\"records\":" + batch + "}";

    bool ok = _postJson(payload);
    if (ok) {
        _storage.removeFirstNFromQueue(n);
        // If more remain, next tick's cadence check will pick up the rest.
    }
    // On failure: leave the queue untouched, retry on the next cadence.
}

void SyncManager::_syncTimeFromNtp() {
    configTime(UTC_OFFSET_SECONDS, 0, "pool.ntp.org", "time.nist.gov");
    time_t nowSec = time(nullptr);
    int attempts = 0;
    // Bounded blocking wait (~2s max) — acceptable since this only runs
    // once per reconnect and then every NTP_RESYNC_INTERVAL_MS.
    while (nowSec < 100000 && attempts < 20) {
        delay(100);
        nowSec = time(nullptr);
        attempts++;
    }
    if (nowSec >= 100000) {
        _rtc.adjust((uint32_t)nowSec);
        _lastNtpSyncMs = millis();
    }
}

void SyncManager::tick() {
    bool connected = _net.isConnected();

    // Reconnect edge: correct RTC drift and kick an immediate flush.
    if (connected && !_wasConnected) {
        _syncTimeFromNtp();
        _flushRequested = true;
    }
    _wasConnected = connected;

    if (connected && (millis() - _lastNtpSyncMs > NTP_RESYNC_INTERVAL_MS)) {
        _syncTimeFromNtp();
    }

    bool queueHasWork = !_storage.queueIsEmpty();
    bool cadenceElapsed = (millis() - _lastFlushAttemptMs) >= SYNC_QUEUE_FLUSH_INTERVAL_MS;

    if (connected && queueHasWork && (_flushRequested || cadenceElapsed)) {
        _lastFlushAttemptMs = millis();
        _flushRequested = false;
        _flushQueueBatch();
    }
}
