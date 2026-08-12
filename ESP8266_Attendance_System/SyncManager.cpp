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
    client.setInsecure();

    HTTPClient https;
    https.setTimeout(SYNC_HTTP_TIMEOUT_MS);
    https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS); // Clean 2-step handling prevents BearSSL socket reuse errors (-1)

    if (!https.begin(client, SYNC_ENDPOINT_URL)) {
        Serial.println(F("[SYNC] Failed to begin HTTPS connection."));
        return false;
    }
    https.addHeader("Content-Type", "application/json");

    int httpCode = https.POST(jsonBody);
    bool ok = false;
    Serial.print(F("[SYNC] HTTP POST code: "));
    Serial.println(httpCode);

    if (httpCode == 200) {
        String resp = https.getString();
        Serial.print(F("[SYNC] Response: "));
        Serial.println(resp);
        ok = (resp.indexOf("\"status\":\"ok\"") >= 0);
        https.end();
    } else if (httpCode == 302 || httpCode == 301 || httpCode == 307) {
        // HTTP 302 means Google Apps Script executed doPost() successfully!
        ok = true;
        String redirectUrl = https.getLocation();
        https.end(); // Close 1st TLS connection cleanly before opening 2nd to different domain

        if (redirectUrl.length() > 0) {
            WiFiClientSecure client2;
            client2.setInsecure();
            HTTPClient https2;
            https2.setTimeout(SYNC_HTTP_TIMEOUT_MS);
            if (https2.begin(client2, redirectUrl)) {
                int code2 = https2.GET();
                if (code2 == 200) {
                    String resp = https2.getString();
                    Serial.print(F("[SYNC] Redirect response: "));
                    Serial.println(resp);
                }
                https2.end();
            }
        }
    } else {
        https.end();
    }

    if (ok) {
        Serial.println(F("[SYNC] Sync succeeded -> queue cleared."));
    } else {
        Serial.println(F("[SYNC] Sync failed or rejected by server."));
    }

    return ok;
}

bool SyncManager::_sendSingleRecord(const String &uid, const StaffInfo &staff,
                                     AttendanceType type, uint32_t ts) {
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

void SyncManager::enqueue(const AttendanceEvent &event) {
    _storage.appendToQueue(event.uid, event.staff, event.type, event.timestamp);
    _flushRequested = true; // tick() will flush on the next loop iteration if backoff cleared
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
        _flushBackoffMs = 5000; // Reset backoff on success
        _nextFlushAllowedMs = millis();
    } else {
        // Exponential backoff on failure: 5s -> 10s -> 20s -> 40s -> 60s max
        _flushBackoffMs = _flushBackoffMs * 2;
        if (_flushBackoffMs > 60000) _flushBackoffMs = 60000;
        _nextFlushAllowedMs = millis() + _flushBackoffMs;
        Serial.print(F("[SYNC] Flush failed. Next retry in "));
        Serial.print(_flushBackoffMs / 1000);
        Serial.println(F("s."));
    }
}

void SyncManager::syncDailyStateFromCloud() {
    if (!_net.isConnected()) return;

    String todayDate = _rtc.nowDateString();
    if (todayDate == "0000-00-00") return;

    StaticJsonDocument<256> reqDoc;
    reqDoc["action"] = "getDailyState";
    reqDoc["secret"] = SYNC_SHARED_SECRET;
    reqDoc["date"] = todayDate;

    String payload;
    serializeJson(reqDoc, payload);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setTimeout(SYNC_HTTP_TIMEOUT_MS);
    https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

    if (!https.begin(client, SYNC_ENDPOINT_URL)) return;
    https.addHeader("Content-Type", "application/json");

    int code = https.POST(payload);
    String resp = "";

    if (code == 200) {
        resp = https.getString();
        https.end();
    } else if (code == 302 || code == 301 || code == 307) {
        String redirectUrl = https.getLocation();
        https.end(); // Close 1st TLS connection cleanly before opening 2nd

        if (redirectUrl.length() > 0) {
            WiFiClientSecure client2;
            client2.setInsecure();
            HTTPClient https2;
            https2.setTimeout(SYNC_HTTP_TIMEOUT_MS);
            if (https2.begin(client2, redirectUrl)) {
                int code2 = https2.GET();
                if (code2 == 200) {
                    resp = https2.getString();
                }
                https2.end();
            }
        }
    } else {
        https.end();
    }

    if (resp.length() > 0) {
        DynamicJsonDocument doc(DAYSTATE_JSON_CAPACITY);
        DeserializationError err = deserializeJson(doc, resp);
        if (!err && doc["status"] == "ok" && doc.containsKey("states")) {
            JsonObjectConst states = doc["states"].as<JsonObjectConst>();
            _storage.updateDailyStateFromCloud(todayDate, states);
            Serial.print(F("[SYNC] Cloud state synced for "));
            Serial.print(todayDate);
            Serial.print(F(" ("));
            Serial.print(states.size());
            Serial.println(F(" staff records)."));
        }
    }
}

void SyncManager::_startNtpSync() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "africa.pool.ntp.org");
    _ntpSyncPending = true;
    _ntpStartMs = millis();
}

void SyncManager::tick() {
    bool connected = _net.isConnected();

    // Reconnect edge: start NTP sync (non-blocking) and set flush flag.
    if (connected && !_wasConnected) {
        _startNtpSync();
        _flushRequested = true;
    }
    _wasConnected = connected;

    // Check background NTP response non-blockingly
    if (connected && _ntpSyncPending) {
        time_t utcSec = time(nullptr);
        if (utcSec >= 100000) {
            time_t localSec = utcSec + UTC_OFFSET_SECONDS; // apply WAT (UTC+1)
            _rtc.adjust((uint32_t)localSec);
            _lastNtpSyncMs = millis();
            _ntpSyncPending = false;
            _ntpRetryNeeded = false;
            Serial.print(F("[NTP] RTC synced. UTC="));
            Serial.print((uint32_t)utcSec);
            Serial.print(F(" WAT="));
            Serial.println((uint32_t)localSec);
            _cloudStateSyncPending = true;
        } else if (millis() - _ntpStartMs >= 8000) {
            _ntpSyncPending = false;
            _ntpRetryNeeded = true;
            _ntpRetryAtMs = millis();
            Serial.println(F("[NTP] Sync timed out — will retry in 30 s."));
        }
    }

    if (connected && _cloudStateSyncPending) {
        _cloudStateSyncPending = false;
        syncDailyStateFromCloud();
    }

    // Retry NTP 30 s after a failed attempt.
    if (connected && !_ntpSyncPending && _ntpRetryNeeded &&
        (millis() - _ntpRetryAtMs >= NTP_RETRY_INTERVAL_MS)) {
        _startNtpSync();
    }

    // Periodic drift correction every 6 hours.
    if (connected && !_ntpSyncPending && !_ntpRetryNeeded &&
        (millis() - _lastNtpSyncMs > NTP_RESYNC_INTERVAL_MS)) {
        _startNtpSync();
    }

    bool queueHasWork = !_storage.queueIsEmpty();
    bool cadenceElapsed = (millis() - _lastFlushAttemptMs) >= SYNC_QUEUE_FLUSH_INTERVAL_MS;
    bool backoffCleared = (millis() >= _nextFlushAllowedMs);

    if (connected && queueHasWork && backoffCleared && (_flushRequested || cadenceElapsed)) {
        _lastFlushAttemptMs = millis();
        _flushRequested = false;
        _flushQueueBatch();
    }
}
