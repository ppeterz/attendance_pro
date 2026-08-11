#ifndef RTC_DRIVER_H
#define RTC_DRIVER_H

#include <Arduino.h>
#include <RTClib.h>

// HAL wrapper around the DS3231. This is our single source of truth
// for "what time is it" — used for both online (NTP-corrected) and
// offline (battery-backed) timestamping, so attendance logic never
// has to know or care which mode we're in.
class RTC_Driver {
public:
    bool begin();                     // returns false if RTC not found (wiring fault)
    bool lostPower();                 // true if backup battery died / first boot
    uint32_t nowUnix();               // seconds since epoch
    String nowDateString();           // "YYYY-MM-DD" — used for day-boundary comparisons
    String nowTimeString();           // "HH:MM" — for LCD display
    String nowDateTimeString();       // "YYYY-MM-DD HH:MM:SS" — for logs

    // Set the RTC clock. Call this once WiFi/NTP time is available,
    // so the RTC self-corrects for drift over time.
    void adjust(uint32_t unixTime);

private:
    RTC_DS3231 _rtc;
    bool _available = false;
};

#endif // RTC_DRIVER_H
