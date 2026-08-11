#include "RTC_Driver.h"

bool RTC_Driver::begin() {
    _available = _rtc.begin();
    return _available;
}

bool RTC_Driver::lostPower() {
    if (!_available) return true;
    return _rtc.lostPower();
}

uint32_t RTC_Driver::nowUnix() {
    if (!_available) return 0;
    return _rtc.now().unixtime();
}

String RTC_Driver::nowDateString() {
    if (!_available) return "0000-00-00";
    DateTime n = _rtc.now();
    char buf[11];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", n.year(), n.month(), n.day());
    return String(buf);
}

String RTC_Driver::nowTimeString() {
    if (!_available) return "--:-- --";
    DateTime n = _rtc.now();
    uint8_t h = n.hour();
    const char *ampm = (h < 12) ? "AM" : "PM";
    uint8_t h12 = h % 12;
    if (h12 == 0) h12 = 12;          // midnight/noon → 12, not 0
    char buf[9];
    snprintf(buf, sizeof(buf), "%2d:%02d %s", h12, n.minute(), ampm);
    return String(buf);              // e.g. " 1:47 PM" or "12:47 AM" — always 8 chars
}

String RTC_Driver::nowDateTimeString() {
    if (!_available) return "0000-00-00 00:00:00";
    DateTime n = _rtc.now();
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             n.year(), n.month(), n.day(), n.hour(), n.minute(), n.second());
    return String(buf);
}

void RTC_Driver::adjust(uint32_t unixTime) {
    if (!_available) return;
    _rtc.adjust(DateTime(unixTime));
}
