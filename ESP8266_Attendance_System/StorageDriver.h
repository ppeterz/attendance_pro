#ifndef STORAGE_DRIVER_H
#define STORAGE_DRIVER_H

#include <Arduino.h>
#include "Config.h"

// Full staff record attached to an enrolled card.
struct StaffInfo {
    String firstName;
    String lastName;
    String role;
    String staffId;

    String displayName() const { return firstName + " " + lastName; }
};

// HAL wrapper around LittleFS. Owns three logical stores:
//   1. Enrolled cards map (UID -> StaffInfo)
//   2. Daily check-in/out state (UID -> last type + last date)
//   3. Offline attendance queue (append-only JSON-lines)
// Directive 6: NVS persistence with graceful fallback if storage
// is blank/corrupt (falls back to empty maps, never crashes).
class StorageDriver {
public:
    bool begin(); // mounts LittleFS, creates files if missing

    // ---- Enrolled cards ----
    bool isEnrolled(const String &uid);
    bool getStaffInfo(const String &uid, StaffInfo &outInfo);
    bool enrollCard(const String &uid, const StaffInfo &staff); // add or overwrite

    // ---- Daily check-in/out state ----
    // Given a UID and today's date string, decides whether this scan
    // is a CHECK_IN or CHECK_OUT, updates the persisted state, and
    // returns the resolved type. Day-boundary reset is automatic:
    // if the stored last-date != today, it always resolves to CHECK_IN.
    AttendanceType resolveScanType(const String &uid, const String &todayDate);

    // ---- Offline queue ----
    // Memory-safe by design: never loads the whole queue into RAM.
    // SyncManager drains it in small batches instead.
    bool appendToQueue(const String &uid, const StaffInfo &staff,
                        AttendanceType type, uint32_t unixTimestamp);
    int queueCount();
    bool queueIsEmpty();
    // Reads up to maxRecords lines from the FRONT of the queue and
    // returns them as a JSON array string ready to POST. Does NOT
    // remove them — caller must call removeFirstNFromQueue() only
    // after a confirmed successful sync of that batch.
    String readQueueBatchAsJsonArray(int maxRecords, int &outRecordCount);
    bool removeFirstNFromQueue(int n);
    bool clearQueue(); // drops the whole queue file (used rarely / diagnostics)

private:
    bool _mounted = false;
    bool _fileExistsOrCreate(const char *path, const char *defaultContent);
};

#endif // STORAGE_DRIVER_H
