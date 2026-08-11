#include "AttendanceLogic.h"

AttendanceLogic::AttendanceLogic(RFID_Driver &rfid, RTC_Driver &rtc, StorageDriver &storage)
    : _rfid(rfid), _rtc(rtc), _storage(storage) {}

bool AttendanceLogic::tick(AttendanceEvent &outEvent) {
    String uid;
    if (!_rfid.pollForCard(uid)) {
        return false; // no card present this tick
    }
    _rfid.releaseCard(); // done reading the tag as soon as we have the UID

    uint32_t now = millis();
    uint32_t elapsedSinceLast = now - _lastScanMs;

    // 1. Same-card duplicate tap suppression (5s ignore window for identical UID)
    if (uid == _lastUid && elapsedSinceLast < DUPLICATE_SCAN_IGNORE_MS) {
        outEvent.result = AttendanceEvent::Result::DUPLICATE_IGNORED;
        outEvent.uid = uid;
        return true;
    }

    // 2. Rapid multi-card detection (different card scanned < 2.5s after previous card)
    if (_lastScanMs > 0 && uid != _lastUid && elapsedSinceLast < RAPID_MULTI_CARD_WARN_MS) {
        Serial.print(F("[SECURITY] Rapid multi-card tap detected! ("));
        Serial.print(elapsedSinceLast);
        Serial.println(F(" ms after previous card)"));
    }

    _lastUid = uid;
    _lastScanMs = now;

    StaffInfo staff;
    if (!_storage.getStaffInfo(uid, staff)) {
        outEvent.result = AttendanceEvent::Result::UNKNOWN_CARD;
        outEvent.uid = uid;
        return true;
    }

    String today = _rtc.nowDateString();
    AttendanceType type = _storage.resolveScanType(uid, today);
    if (type == AttendanceType::ALREADY_DONE) {
        outEvent.result = AttendanceEvent::Result::ALREADY_COMPLETED_TODAY;
        outEvent.uid = uid;
        outEvent.staff = staff;
        return true;
    }

    outEvent.result = AttendanceEvent::Result::VALID_SCAN;
    outEvent.uid = uid;
    outEvent.staff = staff;
    outEvent.type = type;
    outEvent.timestamp = _rtc.nowUnix();
    return true;
}
