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
    if (!_available) return "--:--";
    DateTime n = _rtc.now();
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", n.hour(), n.minute());
    return String(buf);
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
