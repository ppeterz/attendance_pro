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
    https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    if (!https.begin(client, SYNC_ENDPOINT_URL)) {
        Serial.println(F("[SYNC] Failed to begin HTTPS connection."));
        return false;
    }
    https.addHeader("Content-Type", "application/json");

    int httpCode = https.POST(jsonBody);
    bool ok = false;
    Serial.print(F("[SYNC] HTTP POST code: "));
    Serial.println(httpCode);

    if (httpCode > 0) {
        String resp = https.getString();
        Serial.print(F("[SYNC] Response: "));
        Serial.println(resp);
        ok = (resp.indexOf("\"status\":\"ok\"") >= 0);
    }

    if (ok) {
        Serial.println(F("[SYNC] Sync succeeded -> queue cleared."));
    } else {
        Serial.println(F("[SYNC] Sync failed or rejected by server."));
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
    https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    if (!https.begin(client, SYNC_ENDPOINT_URL)) return;
    https.addHeader("Content-Type", "application/json");

    int code = https.POST(payload);
    if (code > 0) {
        String resp = https.getString();
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
    https.end();
}

void SyncManager::_syncTimeFromNtp() {
    // Give the network stack 2 s to stabilise after WiFi connect before
    // sending UDP — avoids "Sync failed" on the very first boot attempt.
    delay(2000);

    // Always fetch raw UTC from NTP (configTime offset only affects localtime(),
    // NOT time() — so we apply the WAT offset manually before writing the RTC).
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "africa.pool.ntp.org");
    time_t utcSec = time(nullptr);
    int attempts = 0;
    // Wait up to 5 s for a valid NTP response.
    while (utcSec < 100000 && attempts < 50) {
        delay(100);
        utcSec = time(nullptr);
        attempts++;
    }
    if (utcSec >= 100000) {
        time_t localSec = utcSec + UTC_OFFSET_SECONDS; // apply WAT (UTC+1)
        _rtc.adjust((uint32_t)localSec);
        _lastNtpSyncMs = millis();
        _ntpRetryNeeded = false;
        Serial.print(F("[NTP] RTC synced. UTC="));
        Serial.print((uint32_t)utcSec);
        Serial.print(F(" WAT="));
        Serial.println((uint32_t)localSec);
        syncDailyStateFromCloud();
    } else {
        // Schedule a retry in 30 s instead of waiting the full 6-hour interval.
        _ntpRetryNeeded = true;
        _ntpRetryAtMs = millis();
        Serial.println(F("[NTP] Sync failed — will retry in 30 s."));
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

    // Retry NTP 30 s after a failed attempt.
    if (connected && _ntpRetryNeeded &&
        (millis() - _ntpRetryAtMs >= NTP_RETRY_INTERVAL_MS)) {
        _syncTimeFromNtp();
    }

    // Periodic drift correction every 6 hours.
    if (connected && !_ntpRetryNeeded &&
        (millis() - _lastNtpSyncMs > NTP_RESYNC_INTERVAL_MS)) {
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
