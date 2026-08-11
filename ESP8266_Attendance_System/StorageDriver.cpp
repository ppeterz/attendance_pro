#include "StorageDriver.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

bool StorageDriver::_fileExistsOrCreate(const char *path, const char *defaultContent) {
    if (LittleFS.exists(path)) return true;
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print(defaultContent);
    f.close();
    return true;
}

bool StorageDriver::begin() {
    _mounted = LittleFS.begin();
    if (!_mounted) {
        // Directive 6: fall back gracefully rather than crash.
        // Attempt a format-and-retry once (first boot on fresh flash).
        LittleFS.format();
        _mounted = LittleFS.begin();
    }
    if (!_mounted) return false;

    _fileExistsOrCreate(FS_PATH_ENROLLED_CARDS, "{}");
    _fileExistsOrCreate(FS_PATH_DAILY_STATE, "{}");
    _fileExistsOrCreate(FS_PATH_OFFLINE_QUEUE, "");
    return true;
}

// ---------------- Enrolled cards ----------------

bool StorageDriver::isEnrolled(const String &uid) {
    StaffInfo info;
    return getStaffInfo(uid, info);
}

bool StorageDriver::getStaffInfo(const String &uid, StaffInfo &outInfo) {
    File f = LittleFS.open(FS_PATH_ENROLLED_CARDS, "r");
    if (!f) return false;
    DynamicJsonDocument doc(ENROLLED_JSON_CAPACITY);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false; // corrupt file -> treat as "not enrolled", never crash

    if (!doc.containsKey(uid)) return false;
    JsonObject rec = doc[uid];
    outInfo.firstName = rec["fn"].as<String>();
    outInfo.lastName  = rec["ln"].as<String>();
    outInfo.role      = rec["role"].as<String>();
    outInfo.staffId   = rec["sid"].as<String>();
    return true;
}

bool StorageDriver::enrollCard(const String &uid, const StaffInfo &staff) {
    DynamicJsonDocument doc(ENROLLED_JSON_CAPACITY);
    File fr = LittleFS.open(FS_PATH_ENROLLED_CARDS, "r");
    if (fr) {
        deserializeJson(doc, fr); // ignore error -> start from empty doc on corruption
        fr.close();
    }

    JsonObject rec = doc.containsKey(uid) ? doc[uid].as<JsonObject>() : doc.createNestedObject(uid);
    rec["fn"] = staff.firstName;
    rec["ln"] = staff.lastName;
    rec["role"] = staff.role;
    rec["sid"] = staff.staffId;

    File fw = LittleFS.open(FS_PATH_ENROLLED_CARDS, "w");
    if (!fw) return false;
    bool ok = serializeJson(doc, fw) > 0;
    fw.close();
    return ok;
}

// ---------------- Daily check-in/out state ----------------

AttendanceType StorageDriver::resolveScanType(const String &uid, const String &todayDate) {
    DynamicJsonDocument doc(DAYSTATE_JSON_CAPACITY);
    File fr = LittleFS.open(FS_PATH_DAILY_STATE, "r");
    if (fr) {
        deserializeJson(doc, fr);
        fr.close();
    }

    AttendanceType resolved = AttendanceType::CHECK_IN;

    if (doc.containsKey(uid)) {
        String lastDate = doc[uid]["d"].as<String>();
        uint8_t lastType = doc[uid]["t"].as<uint8_t>();
        uint8_t count = doc[uid].containsKey("c") ? doc[uid]["c"].as<uint8_t>() : (lastDate == todayDate ? 1 : 0);

        if (lastDate == todayDate) {
            if (count >= 2 || lastType == (uint8_t)AttendanceType::CHECK_OUT) {
                // Already checked in AND checked out today!
                return AttendanceType::ALREADY_DONE;
            } else {
                // Was CHECK_IN -> now CHECK_OUT
                resolved = AttendanceType::CHECK_OUT;
            }
        } else {
            // New day reset -> CHECK_IN
            resolved = AttendanceType::CHECK_IN;
        }
    } else {
        // First time ever -> CHECK_IN
        resolved = AttendanceType::CHECK_IN;
    }

    // Persist the new state
    doc[uid]["t"] = (uint8_t)resolved;
    doc[uid]["d"] = todayDate;
    doc[uid]["c"] = (resolved == AttendanceType::CHECK_IN) ? 1 : 2;

    File fw = LittleFS.open(FS_PATH_DAILY_STATE, "w");
    if (fw) {
        serializeJson(doc, fw);
        fw.close();
    }

    return resolved;
}

// ---------------- Offline queue (JSON-lines, append-only) ----------------

bool StorageDriver::appendToQueue(const String &uid, const StaffInfo &staff,
                                   AttendanceType type, uint32_t unixTimestamp) {
    File f = LittleFS.open(FS_PATH_OFFLINE_QUEUE, "a");
    if (!f) return false;

    StaticJsonDocument<SYNC_RECORD_JSON_CAPACITY> rec;
    rec["uid"] = uid;
    rec["fn"] = staff.firstName;
    rec["ln"] = staff.lastName;
    rec["role"] = staff.role;
    rec["sid"] = staff.staffId;
    rec["type"] = (type == AttendanceType::CHECK_IN) ? "IN" : "OUT";
    rec["ts"] = unixTimestamp;

    serializeJson(rec, f);
    f.print("\n");
    f.close();
    return true;
}

int StorageDriver::queueCount() {
    File f = LittleFS.open(FS_PATH_OFFLINE_QUEUE, "r");
    if (!f) return 0;
    int count = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) count++;
    }
    f.close();
    return count;
}

bool StorageDriver::queueIsEmpty() {
    return queueCount() == 0;
}

String StorageDriver::readQueueBatchAsJsonArray(int maxRecords, int &outRecordCount) {
    outRecordCount = 0;
    File f = LittleFS.open(FS_PATH_OFFLINE_QUEUE, "r");
    if (!f) return "[]";

    String arr = "[";
    bool first = true;
    while (f.available() && outRecordCount < maxRecords) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        if (!first) arr += ",";
        arr += line;
        first = false;
        outRecordCount++;
    }
    f.close();
    arr += "]";
    return arr;
}

bool StorageDriver::removeFirstNFromQueue(int n) {
    int total = queueCount();
    if (n >= total) {
        return clearQueue();
    }

    File fr = LittleFS.open(FS_PATH_OFFLINE_QUEUE, "r");
    if (!fr) return false;

    const char *tmpPath = "/queue_tmp.jsonl";
    File fw = LittleFS.open(tmpPath, "w");
    if (!fw) { fr.close(); return false; }

    int skipped = 0;
    while (fr.available()) {
        String line = fr.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        if (skipped < n) {
            skipped++;
            continue; // drop this line (already synced)
        }
        fw.print(line);
        fw.print("\n");
    }
    fr.close();
    fw.close();

    LittleFS.remove(FS_PATH_OFFLINE_QUEUE);
    LittleFS.rename(tmpPath, FS_PATH_OFFLINE_QUEUE);
    return true;
}

bool StorageDriver::clearQueue() {
    File f = LittleFS.open(FS_PATH_OFFLINE_QUEUE, "w"); // truncate
    if (!f) return false;
    f.close();
    return true;
}
