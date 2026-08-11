#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================
// TIER 1: SYSTEM CONFIG & HARDWARE PIN MAP
// ESP8266EX (NodeMCU v3) — 4MB flash, WiFi-only, no hardware
// PWM/LEDC (confirmed from chip readout: software PWM only).
//
// NOTE: pins are raw GPIO numbers, not the Dx silkscreen labels.
// The Dx->GPIO aliases (D0, D1, D2...) are only defined by some
// board-variant files (e.g. "NodeMCU 1.0") and NOT others (e.g.
// "Generic ESP8266 Module"), which causes "'D3' was not declared"
// compile errors depending on which board is selected. Raw GPIO
// numbers work on every ESP8266 board variant, so we use those and
// just comment the matching silkscreen label for wiring reference.
// ============================================================

// ---------- Firmware identity ----------
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.2.1"
#endif
#define DEVICE_ID "ATTEND-01"   // single unit for now; bump if you add more later
                                 // also used as OTA hostname + WiFi setup AP name

// ---------- RC522 RFID reader (hardware SPI) ----------
// NodeMCU hardware SPI pins are fixed (SCK/MOSI/MISO); only SS and
// RST are chosen by us. GPIO15 (D8) has an onboard pull-down on
// NodeMCU boards, which is exactly what SPI SS needs to stay safe
// during boot. GPIO16 (D0) is used for RST because it has no
// boot-strapping role, unlike GPIO0/GPIO2 — safe to drive freely
// after boot.
#define RFID_SS_PIN   2  // D4 / GPIO15 (SDA/SS on RC522)
#define RFID_RST_PIN  16   // D0
// SCK=GPIO14(D5), MOSI=GPIO13(D7), MISO=GPIO12(D6) — fixed HW SPI, no #define needed

// ---------- I2C bus (shared: DS3231 RTC + 16x2 LCD) ----------
#define I2C_SDA_PIN   4    // D2
#define I2C_SCL_PIN   5    // D1
#define LCD_I2C_ADDR  0x27 // change to 0x3F if your backpack uses that address
#define LCD_COLS      16
#define LCD_ROWS      2
// DS3231 address is fixed at 0x68 by the chip itself, no define needed

// ---------- Mode button (single button, 3 gestures) ----------
// Reuses GPIO0 — the same pin as NodeMCU's onboard FLASH button.
// Safe as a button pin because it uses INPUT_PULLUP (idle = HIGH),
// which matches the required boot condition (GPIO0 HIGH = normal boot).
// CAUTION: don't hold this button down while power-cycling the board.
//   short press        -> manual sync (or cancel, if mid-enrollment)
//   long press (1.5s)  -> enter card enrollment mode
//   very long (10s)    -> wipe saved WiFi + reboot into setup hotspot
#define MODE_BUTTON_PIN       0   // D3 / GPIO0

// ---------- Onboard status LED (bonus — zero extra wiring) ----------
// NodeMCU's built-in blue LED lives on GPIO2, active-LOW.
#define STATUS_LED_PIN        2   // D4 / GPIO2 (onboard LED)
#define STATUS_LED_ACTIVE_LOW true

// ---------- Timing / behavior thresholds ----------
#define DUPLICATE_SCAN_IGNORE_MS   5000    // ignore repeat tap of same card within 5s
#define BUTTON_LONG_PRESS_MS       1500    // hold >=1.5s = enrollment mode
#define BUTTON_VERY_LONG_PRESS_MS  10000   // hold >=10s  = wipe WiFi settings
#define BUTTON_DEBOUNCE_MS         40
#define ENROLLMENT_TIMEOUT_MS      60000   // auto-exit enrollment mode if idle 30s
#define IDLE_SCREEN_REFRESH_MS     1000    // refresh clock on idle LCD screen
#define WIFI_RECONNECT_RETRY_MS    5000    // how often to nudge WiFi.reconnect() while dropped
#define SYNC_HTTP_TIMEOUT_MS       10000
#define SYNC_MAX_BATCH_SIZE        25      // records per POST — keeps RAM usage bounded
#define SYNC_QUEUE_FLUSH_INTERVAL_MS  5000UL     // background retry cadence while online + queue non-empty
#define NTP_RESYNC_INTERVAL_MS  (6UL * 3600UL * 1000UL) // re-correct RTC drift every 6h while online
#define NTP_RETRY_INTERVAL_MS   30000UL                 // retry NTP 30 s after a failed sync attempt
#define UTC_OFFSET_SECONDS      3600   // UTC+1 (WAT / West Africa Time). Change to your timezone offset in seconds.

// ---------- WiFi provisioning (WiFiManager captive portal) ----------
// No SSID/password is ever hardcoded. On first boot (or after a WiFi
// reset), the device opens its own hotspot named below — connect to
// it from a phone, a setup page pops up automatically (or browse to
// 192.168.4.1) to pick your real WiFi network and enter the password.
// Leave WIFI_SETUP_AP_PASSWORD empty for an open hotspot (easiest to
// join from a phone); set a password (min 8 chars) if you'd rather
// the setup hotspot itself not be open to anyone nearby.
#define WIFI_SETUP_AP_PASSWORD     ""      // "" = open hotspot
#define WIFI_PORTAL_TIMEOUT_S      180     // give up and reboot after 3 min unconfigured

// ---------- OTA (firmware updates over WiFi via Arduino IDE) ----------
// Once the device is on your WiFi, it'll show up under Tools > Port >
// Network Ports in the Arduino IDE as "<DEVICE_ID> at x.x.x.x". You'll
// be asked for OTA_PASSWORD when you upload that way.
#define OTA_PASSWORD  "REPLACE_WITH_AN_OTA_PASSWORD"

// ---------- GitHub-based OTA (pull updates from a repo, no shared LAN needed) ----------
// Host two files in your repo and serve them raw:
//   version.txt   - plain text, single line, e.g. "1.3.0"
//   firmware.bin  - Sketch -> Export Compiled Binary in the Arduino IDE
// The device periodically compares version.txt against FIRMWARE_VERSION
// above, and if different, downloads + flashes firmware.bin.
//
// SECURITY NOTE: FIRMWARE_VERSION aside, this compiled .bin also
// contains SYNC_SHARED_SECRET and OTA_PASSWORD baked in as plain
// strings. If GITHUB_USE_PRIVATE_REPO is false (public repo), anyone
// can download firmware.bin and pull those secrets out with `strings
// firmware.bin`. Prefer a private repo + token unless you're fine
// rotating those secrets independent of firmware distribution.
#define GITHUB_USE_PRIVATE_REPO   false
#define GITHUB_PAT  ""  // not needed — repo is public
#define GITHUB_VERSION_URL   "https://raw.githubusercontent.com/ppeterz/attendance_pro/main/firmware/version.txt"
#define GITHUB_FIRMWARE_URL  "https://raw.githubusercontent.com/ppeterz/attendance_pro/main/firmware/firmware.bin"
#define GITHUB_OTA_CHECK_INTERVAL_MS  (24UL * 3600UL * 1000UL) // check once a day while connected
// false (default): device only NOTIFIES you a new version exists (Serial
// + you trigger it yourself with the "update" CLI command). true: device
// flashes itself automatically the moment it notices a version mismatch,
// unattended. Leave false until you've tested the pipeline a few times.
#define GITHUB_OTA_AUTO_INSTALL   false

// ---------- JSON document sizing ----------
// Sized generously for a single small-office deployment. Bump these
// (and watch free heap via the "heap" Serial CLI command) if you
// enroll a large number of staff.
#define ENROLLED_JSON_CAPACITY   8192
#define DAYSTATE_JSON_CAPACITY   4096
#define SYNC_RECORD_JSON_CAPACITY 384

// ---------- Storage paths (LittleFS) ----------
#define FS_PATH_ENROLLED_CARDS   "/enrolled.json"   // {uid: {fn,ln,role,sid}}
#define FS_PATH_DAILY_STATE      "/daystate.json"   // {uid: {lastType,lastDate}}
#define FS_PATH_OFFLINE_QUEUE    "/queue.jsonl"      // append-only, one record per line

// Google Apps Script Web App URL (deploy as "Anyone" access, /exec endpoint)
#define SYNC_ENDPOINT_URL   "https://script.google.com/macros/s/AKfycbyZsUb8AIX5rXBkO5K5xXhA3I4kY1Kq5TGEUYEy_bI6xqmCoHvt8hqLK5t0crKMXPXgAQ/exec"
// Simple shared-secret so randoms can't POST junk into your sheet
#define SYNC_SHARED_SECRET  "59cfe9e0f6ef676a8efe4e6c384de8a76682d78f7d48c0c7"

// ---------- Attendance record types ----------
enum class AttendanceType : uint8_t { CHECK_IN = 0, CHECK_OUT = 1 };

#endif // CONFIG_H
