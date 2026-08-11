// ============================================================
// ESP8266 RFID Attendance System
// NodeMCU v3 + RC522 + DS3231 + I2C LCD, battery-powered,
// online-live-sync / offline-queue to Google Sheets via Apps Script.
// WiFi setup via phone (WiFiManager captive portal), OTA via WiFi.
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <ESP8266WiFi.h> // needed here for WiFi.localIP() in printStatus()
#include "Config.h"

#include "RFID_Driver.h"
#include "RTC_Driver.h"
#include "LCD_Driver.h"
#include "StorageDriver.h"
#include "ButtonDriver.h"
#include "AttendanceLogic.h"
#include "NetworkManager.h"
#include "SyncManager.h"
#include "OTAManager.h"
#include "GithubOtaManager.h"
#include "UIManager.h"

// ---------------- Global instances (Tier 1 composition root) ----------------
RFID_Driver rfid;
RTC_Driver rtcDriver;
LCD_Driver lcd;
StorageDriver storage;
ButtonDriver button;
AttendanceLogic attendanceLogic(rfid, rtcDriver, storage);
NetworkManager network;
SyncManager sync(storage, rtcDriver, network);
UIManager uiManager(lcd, rtcDriver);
OTAManager ota(uiManager);
GithubOtaManager githubOta(uiManager);

// ---------------- Enrollment session state ----------------
enum class EnrollSubState {
    NONE,
    AWAITING_CARD,
    AWAITING_FIRST_NAME,
    AWAITING_LAST_NAME,
    AWAITING_ROLE,
    AWAITING_STAFF_ID,
    SAVED_CONFIRM
};
EnrollSubState enrollSubState = EnrollSubState::NONE;
String pendingEnrollUid;
StaffInfo pendingStaff;
uint32_t enrollModeEnteredAt = 0;
uint32_t enrollSubStateEnteredAt = 0;

// ---------------- WiFi setup screen edge-detection ----------------
bool wifiSetupScreenShown = false;

// ---------------- Serial CLI ----------------
String serialLineBuffer;

void ledSet(bool on) {
    bool level = on ^ STATUS_LED_ACTIVE_LOW;
    digitalWrite(STATUS_LED_PIN, level ? HIGH : LOW);
}

void updateStatusLed() {
    static uint32_t lastToggle = 0;
    static bool ledState = false;
    uint32_t now = millis();

    switch (network.state()) {
        case NetworkManager::State::CONNECTED:
            ledSet(true);
            break;
        case NetworkManager::State::CONNECTING:
        case NetworkManager::State::CONFIG_PORTAL:
            if (now - lastToggle >= 250) { lastToggle = now; ledState = !ledState; ledSet(ledState); }
            break;
        default: // DISCONNECTED
            if (now - lastToggle >= 1000) { lastToggle = now; ledState = !ledState; ledSet(ledState); }
            break;
    }
}

void printHelp() {
    Serial.println(F("Commands:"));
    Serial.println(F("  status      - show system status"));
    Serial.println(F("  heap        - show free heap memory"));
    Serial.println(F("  resetwifi   - wipe saved WiFi + reboot into phone setup mode"));
    Serial.println(F("  checkupdate - check GitHub for a newer firmware version now"));
    Serial.println(F("  update      - download + flash the newer version (after checkupdate finds one)"));
    Serial.println(F("  help        - this message"));
    Serial.println(F("Button gestures:"));
    Serial.println(F("  short press   - manual sync (or cancel, if mid-enrollment)"));
    Serial.println(F("  double tap    - check & install firmware update from GitHub"));
    Serial.println(F("  hold 1.5s     - enter card enrollment mode"));
    Serial.println(F("  hold 4s       - check & install firmware update from GitHub"));
    Serial.println(F("  hold 10s      - wipe saved WiFi + reboot into phone setup mode"));
}

void printStatus() {
    const char *netStateStr = "?";
    switch (network.state()) {
        case NetworkManager::State::CONFIG_PORTAL: netStateStr = "CONFIG_PORTAL (waiting for phone setup)"; break;
        case NetworkManager::State::CONNECTING:     netStateStr = "CONNECTING"; break;
        case NetworkManager::State::CONNECTED:      netStateStr = "CONNECTED"; break;
        case NetworkManager::State::DISCONNECTED:   netStateStr = "DISCONNECTED"; break;
    }
    Serial.println(F("---- Status ----"));
    Serial.print(F("Firmware: ")); Serial.println(FIRMWARE_VERSION);
    Serial.print(F("WiFi: ")); Serial.println(netStateStr);
    if (network.isConnected()) {
        Serial.print(F("IP: ")); Serial.println(WiFi.localIP());
        Serial.print(F("OTA hostname: ")); Serial.println(DEVICE_ID);
    }
    Serial.print(F("RTC time: ")); Serial.println(rtcDriver.nowDateTimeString());
    Serial.print(F("Offline queue: ")); Serial.print(sync.pendingCount()); Serial.println(F(" record(s) pending"));
    Serial.print(F("Free heap: ")); Serial.print(ESP.getFreeHeap()); Serial.println(F(" bytes"));
    Serial.print(F("Uptime: ")); Serial.print(millis() / 1000); Serial.println(F(" s"));
    if (githubOta.updateAvailable()) {
        Serial.print(F("GitHub OTA: update available -> v"));
        Serial.println(githubOta.availableVersion());
    } else {
        Serial.println(F("GitHub OTA: up to date (as of last check)"));
    }
    Serial.println(F("----------------"));
}

// ---------------- Enrollment field entry (Serial CLI drives this) ----------------

void abortEnrollment(const __FlashStringHelper *reason) {
    Serial.println(reason);
    uiManager.exitEnrollmentMode();
    enrollSubState = EnrollSubState::NONE;
    pendingStaff = StaffInfo();
}

void handleEnrollmentFieldEntry(const String &line) {
    if (line.equalsIgnoreCase("cancel")) {
        abortEnrollment(F("[Enrollment] Cancelled."));
        return;
    }

    switch (enrollSubState) {
        case EnrollSubState::AWAITING_FIRST_NAME:
            pendingStaff.firstName = line;
            enrollSubState = EnrollSubState::AWAITING_LAST_NAME;
            uiManager.showEnrollmentFieldPrompt("Last Name");
            Serial.println(F("Type LAST NAME + Enter (or 'cancel'):"));
            break;

        case EnrollSubState::AWAITING_LAST_NAME:
            pendingStaff.lastName = line;
            enrollSubState = EnrollSubState::AWAITING_ROLE;
            uiManager.showEnrollmentFieldPrompt("Role");
            Serial.println(F("Type ROLE, e.g. Teacher/Admin + Enter (or 'cancel'):"));
            break;

        case EnrollSubState::AWAITING_ROLE:
            pendingStaff.role = line;
            enrollSubState = EnrollSubState::AWAITING_STAFF_ID;
            uiManager.showEnrollmentFieldPrompt("Staff ID");
            Serial.println(F("Type STAFF ID + Enter (or 'cancel'):"));
            break;

        case EnrollSubState::AWAITING_STAFF_ID: {
            pendingStaff.staffId = line;
            bool ok = storage.enrollCard(pendingEnrollUid, pendingStaff);
            if (ok) {
                Serial.println(F("[Enrollment] Saved:"));
                Serial.print(F("  UID:      ")); Serial.println(pendingEnrollUid);
                Serial.print(F("  Name:     ")); Serial.println(pendingStaff.displayName());
                Serial.print(F("  Role:     ")); Serial.println(pendingStaff.role);
                Serial.print(F("  Staff ID: ")); Serial.println(pendingStaff.staffId);
                uiManager.showEnrollmentSaved(pendingStaff.displayName());
                enrollSubState = EnrollSubState::SAVED_CONFIRM;
                enrollSubStateEnteredAt = millis();
            } else {
                Serial.println(F("[Enrollment] FAILED to save (flash write error). Retype the Staff ID to try again, or 'cancel'."));
            }
            break;
        }

        default:
            break;
    }
}

void processSerialLine(const String &rawLine) {
    String line = rawLine;
    line.trim();
    if (line.length() == 0) return;

    bool awaitingField =
        enrollSubState == EnrollSubState::AWAITING_FIRST_NAME ||
        enrollSubState == EnrollSubState::AWAITING_LAST_NAME ||
        enrollSubState == EnrollSubState::AWAITING_ROLE ||
        enrollSubState == EnrollSubState::AWAITING_STAFF_ID;

    if (awaitingField) {
        handleEnrollmentFieldEntry(line);
        return;
    }

    if (line == "status") {
        printStatus();
    } else if (line == "heap") {
        Serial.print(F("Free heap: "));
        Serial.print(ESP.getFreeHeap());
        Serial.println(F(" bytes"));
    } else if (line == "resetwifi") {
        Serial.println(F("Wiping saved WiFi and rebooting into setup mode..."));
        network.resetWifiSettings();
    } else if (line == "checkupdate") {
        githubOta.checkNow();
    } else if (line == "update") {
        if (githubOta.updateAvailable()) {
            githubOta.installNow();
        } else {
            Serial.println(F("No known update pending — run 'checkupdate' first."));
        }
    } else if (line == "help") {
        printHelp();
    } else {
        Serial.println(F("Unknown command. Type 'help'."));
    }
}

void readSerialCli() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            processSerialLine(serialLineBuffer);
            serialLineBuffer = "";
        } else if (c != '\r') {
            serialLineBuffer += c;
        }
    }
}

// ---------------- Enrollment mode driver ----------------

void enterEnrollment() {
    uiManager.enterEnrollmentMode();
    enrollSubState = EnrollSubState::AWAITING_CARD;
    pendingStaff = StaffInfo();
    enrollModeEnteredAt = millis();
    Serial.println(F("[Enrollment] Mode entered. Tap the new card now (or wait 30s to time out)."));
}

void tickEnrollment() {
    uint32_t now = millis();

    switch (enrollSubState) {
        case EnrollSubState::AWAITING_CARD: {
            String uid;
            if (rfid.pollForCard(uid)) {
                rfid.releaseCard();
                pendingEnrollUid = uid;
                enrollSubState = EnrollSubState::AWAITING_FIRST_NAME;
                uiManager.showEnrollmentFieldPrompt("First Name");
                Serial.print(F("[Enrollment] Card UID: "));
                Serial.println(uid);
                Serial.println(F("Type FIRST NAME + Enter (or 'cancel'):"));
            }
            if (now - enrollModeEnteredAt > ENROLLMENT_TIMEOUT_MS) {
                abortEnrollment(F("[Enrollment] Timed out waiting for a card."));
            }
            break;
        }
        case EnrollSubState::AWAITING_FIRST_NAME:
        case EnrollSubState::AWAITING_LAST_NAME:
        case EnrollSubState::AWAITING_ROLE:
        case EnrollSubState::AWAITING_STAFF_ID:
            if (now - enrollModeEnteredAt > ENROLLMENT_TIMEOUT_MS) {
                abortEnrollment(F("[Enrollment] Timed out waiting for field entry."));
            }
            break;
        case EnrollSubState::SAVED_CONFIRM:
            if (now - enrollSubStateEnteredAt > 2000) {
                uiManager.exitEnrollmentMode();
                enrollSubState = EnrollSubState::NONE;
                pendingStaff = StaffInfo();
            }
            break;
        case EnrollSubState::NONE:
            break;
    }
}

// ---------------- WiFi setup screen driver ----------------

void tickWifiSetupScreen() {
    bool portalActive = (network.state() == NetworkManager::State::CONFIG_PORTAL);
    if (portalActive && !wifiSetupScreenShown) {
        uiManager.showWifiSetupMode(network.setupApName());
        wifiSetupScreenShown = true;
        Serial.print(F("[WiFi] Setup hotspot open: "));
        Serial.println(network.setupApName());
        Serial.println(F("Connect to it from a phone, a setup page should pop up"));
        Serial.println(F("(or browse to 192.168.4.1) to pick your WiFi and enter the password."));
    } else if (!portalActive && wifiSetupScreenShown) {
        uiManager.exitWifiSetupMode();
        wifiSetupScreenShown = false;
    }
}

// ---------------- Arduino entry points ----------------

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("=== ESP8266 RFID Attendance System ==="));
    Serial.print(F("Firmware v")); Serial.println(FIRMWARE_VERSION);

    pinMode(STATUS_LED_PIN, OUTPUT);
    ledSet(false);

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); // shared bus: DS3231 + LCD

    bool storageOk = storage.begin();
    bool rtcOk = rtcDriver.begin();
    uiManager.begin();
    rfid.begin();
    bool rfidOk = rfid.selfTest();
    button.begin();
    ota.begin();
    githubOta.begin();
    network.begin(); // may block briefly trying saved creds, then opens the portal if needed
    sync.begin();

    Serial.println(storageOk ? F("[OK] LittleFS mounted") : F("[FAIL] LittleFS mount failed"));
    Serial.println(rtcOk ? F("[OK] DS3231 RTC found") : F("[FAIL] RTC not responding - check I2C wiring"));
    if (rtcOk && rtcDriver.lostPower()) {
        Serial.println(F("[WARN] RTC lost power (dead coin cell or first boot) - "
                          "time is wrong until WiFi/NTP sync runs."));
    }
    Serial.println(rfidOk ? F("[OK] RC522 responding") : F("[FAIL] RC522 not responding - check SPI wiring"));

    if (network.state() == NetworkManager::State::CONFIG_PORTAL) {
        Serial.print(F("[SETUP] No saved WiFi. Setup hotspot: "));
        Serial.println(network.setupApName());
        Serial.println(F("        Connect to it from a phone to configure WiFi."));
    }
    printHelp();
}

void loop() {
    network.tick();
    sync.tick();
    ota.tick(network.isConnected());
    githubOta.tick(network.isConnected());
    updateStatusLed();
    readSerialCli();
    tickWifiSetupScreen();

    ButtonEvent btnEvent = button.tick();
    if (btnEvent == ButtonEvent::DOUBLE_PRESS || btnEvent == ButtonEvent::OTA_PRESS) {
        Serial.println(F("[BUTTON] OTA Update gesture detected -> Checking GitHub for update"));
        if (!uiManager.inEnrollmentMode()) {
            githubOta.triggerUpdateFromButton();
        }
    } else if (btnEvent == ButtonEvent::LONG_PRESS) {
        Serial.println(F("[BUTTON] Long press detected -> Entering Enrollment Mode"));
        if (!uiManager.inEnrollmentMode()) {
            enterEnrollment();
        }
    } else if (btnEvent == ButtonEvent::VERY_LONG_PRESS) {
        Serial.println(F("[BUTTON] Very long press detected -> Resetting WiFi"));
        if (!uiManager.inEnrollmentMode()) {
            Serial.println(F("[WiFi] Reset triggered via button hold. Wiping and rebooting..."));
            uiManager.showWifiResetting();
            delay(1500); // brief, deliberate — device reboots right after anyway
            network.resetWifiSettings();
        }
    } else if (btnEvent == ButtonEvent::SHORT_PRESS) {
        Serial.println(F("[BUTTON] Short press detected -> Requesting manual sync"));
        if (uiManager.inEnrollmentMode()) {
            abortEnrollment(F("[Enrollment] Cancelled."));
        } else {
            sync.requestManualSync();
            uiManager.showManualSyncTriggered();
        }
    }

    if (uiManager.inEnrollmentMode()) {
        tickEnrollment();
    } else if (network.state() != NetworkManager::State::CONFIG_PORTAL) {
        AttendanceEvent event;
        if (attendanceLogic.tick(event)) {
            uiManager.showScanResult(event);
            if (event.result == AttendanceEvent::Result::VALID_SCAN) {
                sync.sendOrQueue(event);
            }
        }
    }

    uiManager.tick(sync.pendingCount());
}
