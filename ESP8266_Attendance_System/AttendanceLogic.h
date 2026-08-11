#ifndef ATTENDANCE_LOGIC_H
#define ATTENDANCE_LOGIC_H

#include <Arduino.h>
#include "Config.h"
#include "RFID_Driver.h"
#include "RTC_Driver.h"
#include "StorageDriver.h"

struct AttendanceEvent {
    enum class Result : uint8_t { VALID_SCAN, DUPLICATE_IGNORED, UNKNOWN_CARD, ALREADY_COMPLETED_TODAY };
    Result result;
    String uid;
    StaffInfo staff;       // populated only for VALID_SCAN
    AttendanceType type;    // meaningful only for VALID_SCAN
    uint32_t timestamp;     // meaningful only for VALID_SCAN
};

// Tier 4: Application/business logic. Owns the "what does this scan
// mean" decision — duplicate suppression, unknown-card detection,
// and IN/OUT resolution. Talks to HAL only through their interfaces
// (Directive 3) and produces a single event per tick for the caller
// to route to UI/Network (Directive 11 — no direct cross-tier calls).
class AttendanceLogic {
public:
    AttendanceLogic(RFID_Driver &rfid, RTC_Driver &rtc, StorageDriver &storage);

    // Call every loop tick. Returns true and fills outEvent if a card
    // was scanned this tick; false otherwise (non-blocking).
    bool tick(AttendanceEvent &outEvent);

private:
    RFID_Driver &_rfid;
    RTC_Driver &_rtc;
    StorageDriver &_storage;

    String _lastUid;
    uint32_t _lastScanMs = 0;
};

#endif // ATTENDANCE_LOGIC_H
