#include "UIManager.h"

UIManager::UIManager(LCD_Driver &lcd, RTC_Driver &rtc) : _lcd(lcd), _rtc(rtc) {}

void UIManager::begin() {
    _lcd.begin();
    _lcd.showTwoLines("SYSTEM STARTING", "Please wait...");
}

void UIManager::finishStartup() {
    _lcd.showTwoLines("System Ready", "Starting up...");
    delay(1000);
    _showIdle(0);
}

void UIManager::_showIdle(int pendingQueueCount) {
    String line1 = "Ready   " + _rtc.nowTimeString();
    String line2 = (pendingQueueCount > 0)
                        ? ("Tap card  Q:" + String(pendingQueueCount))
                        : "Tap your card";
    _lcd.showTwoLines(line1, line2);
}

void UIManager::_showTransient(const String &line1, const String &line2, uint32_t durationMs) {
    _showingTransient = true;
    _transientShownAtMs = millis();
    _transientDurationMs = durationMs;
    _lcd.showTwoLines(line1, line2);
}

void UIManager::tick(int pendingQueueCount) {
    if (_mode != Mode::NORMAL) return; // enrollment/wifi-setup/OTA screens are explicit, not auto-managed here

    uint32_t now = millis();
    if (_showingTransient) {
        if (now - _transientShownAtMs < _transientDurationMs) {
            return; // still within the display window, leave it on screen
        }
        _showingTransient = false;
    }

    if (now - _lastIdleRefreshMs >= IDLE_SCREEN_REFRESH_MS || pendingQueueCount != _lastPendingCount) {
        _lastIdleRefreshMs = now;
        _lastPendingCount = pendingQueueCount;
        _showIdle(pendingQueueCount);
    }
}

void UIManager::showScanResult(const AttendanceEvent &event) {
    String line1, line2;
    switch (event.result) {
        case AttendanceEvent::Result::VALID_SCAN: {
            String typeStr = (event.type == AttendanceType::CHECK_IN) ? "IN " : "OUT ";
            line1 = event.staff.firstName;
            line2 = typeStr + _rtc.nowTimeString();
            break;
        }
        case AttendanceEvent::Result::ALREADY_COMPLETED_TODAY:
            line1 = event.staff.firstName;
            line2 = "Done for today";
            break;
        case AttendanceEvent::Result::DUPLICATE_IGNORED:
            line1 = "Already scanned";
            line2 = "One moment...";
            break;
        case AttendanceEvent::Result::UNKNOWN_CARD:
            line1 = "Unknown card";
            line2 = "See admin";
            break;
    }
    _showTransient(line1, line2, 2000);
}

void UIManager::showManualSyncTriggered() {
    _showTransient("Sync requested", "Check Q: shortly", 1500);
}

void UIManager::showTransientMessage(const String &line1, const String &line2, uint32_t durationMs) {
    _showTransient(line1, line2, durationMs);
}

// ---------------- Enrollment flow ----------------

void UIManager::enterEnrollmentMode() {
    _mode = Mode::ENROLLMENT;
    showEnrollmentPrompt();
}

void UIManager::exitEnrollmentMode() {
    _mode = Mode::NORMAL;
    _showingTransient = false; // force an immediate idle repaint next tick
    _lastPendingCount = -1;
}

bool UIManager::inEnrollmentMode() const {
    return _mode == Mode::ENROLLMENT;
}

void UIManager::showEnrollmentPrompt() {
    _lcd.showTwoLines("ENROLL MODE", "Tap new card...");
}

void UIManager::showEnrollmentFieldPrompt(const String &fieldLabel) {
    _lcd.showTwoLines("Enter " + fieldLabel, "via Serial+Enter");
}

void UIManager::showEnrollmentSaved(const String &displayName) {
    _lcd.showTwoLines("Saved:", displayName);
}

void UIManager::showEnrollmentTimeout() {
    _lcd.showTwoLines("Enroll timeout", "Returning...");
}

// ---------------- WiFi setup (captive portal) ----------------

void UIManager::showWifiSetupMode(const String &apName) {
    _mode = Mode::WIFI_SETUP;
    _lcd.showTwoLines("WiFi Setup Mode", apName);
}

void UIManager::exitWifiSetupMode() {
    _mode = Mode::NORMAL;
    _showingTransient = false;
    _lastPendingCount = -1;
}

void UIManager::showWifiResetting() {
    // Deliberately NOT gated by _mode — the device reboots right after
    // this call, so there's no "next tick" to worry about overwriting it.
    _lcd.showTwoLines("WiFi Reset", "Restarting...");
}

// ---------------- OTA update ----------------

void UIManager::showOtaStart() {
    _mode = Mode::OTA_UPDATE;
    _lcd.showTwoLines("OTA UPDATE", "Starting...");
}

void UIManager::showOtaProgress(int percent) {
    _lcd.showTwoLines("Updating " + String(percent) + "%", "Do not power off");
}

void UIManager::showOtaEnd(bool success) {
    _lcd.showTwoLines(success ? "Update OK" : "Update FAILED",
                       success ? "Rebooting..." : "Check Serial log");
    _mode = Mode::NORMAL; // if it succeeded, device restarts itself almost immediately anyway
}
