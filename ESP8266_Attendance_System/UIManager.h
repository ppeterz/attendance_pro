#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <Arduino.h>
#include "Config.h"
#include "LCD_Driver.h"
#include "RTC_Driver.h"
#include "AttendanceLogic.h"

// Tier 5: owns everything shown on the LCD.
//   NORMAL      - idle clock screen, briefly interrupted by scan-result
//                 messages that auto-revert (non-blocking timer).
//   ENROLLMENT  - guided card-enrollment flow, explicitly driven by
//                 main.cpp rather than auto-reverting.
//   WIFI_SETUP  - shown while the WiFi captive portal is open.
//   OTA_UPDATE  - shown during a firmware update in progress.
class UIManager {
public:
    UIManager(LCD_Driver &lcd, RTC_Driver &rtc);

    void begin();
    void tick(int pendingQueueCount); // call every loop; no-op outside NORMAL mode

    void showScanResult(const AttendanceEvent &event);
    void showManualSyncTriggered();

    // ---- Enrollment ----
    void enterEnrollmentMode();
    void exitEnrollmentMode();
    bool inEnrollmentMode() const;
    void showEnrollmentPrompt();
    void showEnrollmentFieldPrompt(const String &fieldLabel);
    void showEnrollmentSaved(const String &displayName);
    void showEnrollmentTimeout();

    // ---- WiFi setup (captive portal) ----
    void showWifiSetupMode(const String &apName);
    void exitWifiSetupMode();
    void showWifiResetting();

    // ---- OTA update ----
    void showOtaStart();
    void showOtaProgress(int percent);
    void showOtaEnd(bool success);

private:
    enum class Mode : uint8_t { NORMAL, ENROLLMENT, WIFI_SETUP, OTA_UPDATE };

    LCD_Driver &_lcd;
    RTC_Driver &_rtc;

    Mode _mode = Mode::NORMAL;
    bool _showingTransient = false;
    uint32_t _transientShownAtMs = 0;
    uint32_t _transientDurationMs = 2000;

    uint32_t _lastIdleRefreshMs = 0;
    int _lastPendingCount = -1;

    void _showIdle(int pendingQueueCount);
    void _showTransient(const String &line1, const String &line2, uint32_t durationMs);
};

#endif // UI_MANAGER_H
