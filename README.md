# ESP8266 RFID Attendance System — Arduino IDE version

Battery-powered, works offline, live-syncs to Google Sheets when online,
queues and batch-syncs when it isn't. WiFi is set up from your phone
(no laptop needed after the first flash), and firmware updates after
that can go over WiFi too.

This is a flat Arduino IDE sketch (no subfolders — Arduino IDE only
compiles files directly inside the sketch folder). Open
`ESP8266_Attendance_System/ESP8266_Attendance_System.ino` and every
other `.h`/`.cpp` in that same folder loads automatically as tabs.

## Hardware (you already have this)

| Part | Notes |
|---|---|
| NodeMCU v3 (ESP8266EX, 4MB flash) | confirmed via chip readout |
| RC522 RFID reader | SPI |
| DS3231 RTC module | I2C, battery-backed (keep its coin cell fresh) |
| 16x2 I2C LCD (PCF8574 backpack) | address 0x27 by default |
| 1x momentary push button | 3 gestures — see below |
| USB power bank (10,000mAh+ recommended) | primary power — see Power notes |

## Wiring

**RC522 (hardware SPI)**
| RC522 pin | NodeMCU pin | GPIO |
|---|---|---|
| SDA (SS) | D8 | GPIO15 |
| SCK | D5 | GPIO14 |
| MOSI | D7 | GPIO13 |
| MISO | D6 | GPIO12 |
| RST | D0 | GPIO16 |
| 3.3V | 3V3 | — |
| GND | GND | — |

**I2C bus (DS3231 + LCD share this bus, no conflict)**
| Signal | NodeMCU pin | GPIO |
|---|---|---|
| SDA | D2 | GPIO4 |
| SCL | D1 | GPIO5 |

**Mode button** — one leg to D3 (GPIO0), other leg to GND. Same pin as
NodeMCU's onboard FLASH button, so you can reuse that instead of
wiring a new one. Don't hold it down while power-cycling the board
(GPIO0 low at boot = flash mode).

**Status LED** — no wiring needed. Reuses NodeMCU's onboard blue LED
(GPIO2): solid = WiFi connected, fast blink = connecting/setup portal
open, slow blink = disconnected.

**Button gestures:**
| Gesture | Action |
|---|---|
| Short press | Manual sync (or cancel, if mid-enrollment) |
| Hold 1.5s | Enter card enrollment mode |
| Hold 10s | Wipe saved WiFi + reboot into phone setup mode |

## 1. Arduino IDE setup

**Add the ESP8266 board package** (skip if already installed):
1. File → Preferences → "Additional Boards Manager URLs":
   `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
2. Tools → Board → Boards Manager → search "esp8266" → install.

**Select the board:**
- Tools → Board → esp8266 → **NodeMCU 1.0 (ESP-12E Module)**
- Tools → Flash Size → **must be an "OTA" option now** —
  **"4MB (FS:2MB OTA:~1019KB)"**. This reserves two firmware slots so
  OTA can write the new image while the old one keeps running. A
  non-OTA flash size option will cause OTA uploads to fail.
- Tools → Upload Speed → 921600

**Install libraries** (Sketch → Include Library → Manage Libraries):
| Library | Author | Used for |
|---|---|---|
| MFRC522 | GithubCommunity / miguelbalboa | RC522 reader |
| RTClib | Adafruit | DS3231 |
| LiquidCrystal I2C | Marco Schwartz (Frank de Brabander fork also works) | 16x2 LCD |
| ArduinoJson | Benoit Blanchon | **install version 6.x**, not v7 — code uses the v6 API |
| WiFiManager | tzapu | phone-based WiFi setup (captive portal) |

ArduinoOTA doesn't need installing — it ships with the ESP8266 board
package.

No LittleFS data-upload plugin needed — the firmware creates its own
storage files on first boot automatically.

## 2. Set up the Google Sheet

1. Create a new Google Sheet (or use an existing one).
2. Extensions → Apps Script.
3. Delete the placeholder code, paste in `AppsScript/Code.gs`.
4. Change `SHARED_SECRET` at the top to a long random string —
   e.g. `openssl rand -hex 24`.
5. Deploy → New deployment → type **Web app**.
   - Execute as: **Me**
   - Who has access: **Anyone**
   (Has to be "Anyone" since the ESP8266 can't do Google OAuth — the
   shared secret is what actually protects the endpoint.)
6. Copy the `/exec` URL it gives you.

## 3. Configure the firmware

Open `Config.h` (a tab in the IDE) and set:
- `SYNC_ENDPOINT_URL` → the `/exec` URL from step 2
- `SYNC_SHARED_SECRET` → the **same** string you put in `Code.gs`
- `OTA_PASSWORD` → any password you'll remember, used when uploading
  over WiFi later

Leave WiFi alone entirely — it's configured from your phone after
flashing, not in code.

## 4. Flash and first boot (WiFi setup from your phone)

1. Connect via USB, Verify/Upload from the Arduino IDE (this first
   upload has to be over USB — OTA only becomes available after the
   device successfully joins a WiFi network).
2. Tools → Serial Monitor, baud **115200**, line ending **Newline**.
3. You'll see self-test results for LittleFS / RTC / RC522. Fix any
   `[FAIL]` before continuing.
4. Since there's no saved WiFi yet, the device opens its own hotspot
   — LCD shows `WiFi Setup Mode` / `ATTEND-01-Setup` (or whatever you
   set `DEVICE_ID` to in Config.h), and Serial prints the same.
5. On your **phone**: open WiFi settings, join that hotspot. A "Sign
   in to network" / setup page should pop up automatically (Android
   and iOS both do this for captive portals). If it doesn't pop up,
   open a browser and go to `192.168.4.1`.
6. On that page, tap **Configure WiFi**, pick your real network from
   the scanned list, enter its password, save.
7. The device reconnects to your real WiFi, the hotspot closes, LCD
   returns to the normal idle screen. Done — it reconnects
   automatically on every future power-up, no phone needed again
   unless you reset it.
8. Type `status` in Serial any time to check WiFi state, IP, queue
   count, RTC time, free heap.

**To reconfigure WiFi later** (new router, moved office, etc.): either
hold the mode button for 10 seconds, or type `resetwifi` in Serial —
both wipe the saved network and reboot straight back into step 4-6
above.

## 5. Enroll staff cards

Each card stores a full staff record: **first name, last name, role,
and staff ID** — all four go to the Sheet on every scan.

1. Hold the mode button ~1.5s → LCD shows `ENROLL MODE`.
2. Tap the new card → LCD prompts `Enter First Name`.
3. In the Serial Monitor, type each field and press Enter, in order:
   First Name, Last Name, Role (e.g. `Teacher`), Staff ID.
4. LCD confirms `Saved: <First Last>`, returns to idle after ~2s.
5. Type `cancel` at any prompt to abort. If the final save fails
   (flash write error), you'll be asked to retype just the Staff ID.
6. Enrollment auto-exits after 30s of inactivity at any step.

One card per staff member — re-enrolling an existing UID overwrites
all four fields.

## 6. Normal operation

- **Idle screen**: current time, "Tap your card" (or `Q:N` if N
  records are waiting to sync).
- **Valid scan**: staff first name + `IN`/`OUT` + time (last name,
  role, staff ID go to the Sheet, not the 16-char LCD). Toggles
  automatically per card, resets at midnight via RTC date comparison
  — survives power loss/battery swaps cleanly.
- **Duplicate tap** (same card within 5s): ignored, "Already
  scanned".
- **Unknown card**: "Unknown card / See admin" — not logged until
  enrolled.
- **Manual sync**: short-press the button any time to force a sync
  attempt.

## 7. Offline behavior

- WiFi up → each scan sent immediately.
- WiFi down (or send fails) → queued to flash, nothing lost.
- Auto-flushes on reconnect, retries every 5s in the background while
  anything's pending, batches of 25 records at a time. Matches your
  stated worst case: a few hours up to 2 days offline.

## 8. Firmware updates over WiFi — two ways

### 8a. Direct from Arduino IDE (same LAN only)

Once the device has joined your WiFi at least once:

1. With the device powered on and connected, open the Arduino IDE.
2. Tools → Port → look under **"Network Ports"** for an entry like
   `ATTEND-01 at 192.168.x.x`. Select it.
3. Verify/Upload as normal. You'll be prompted for `OTA_PASSWORD`
   (whatever you set in `Config.h`).
4. LCD shows `OTA UPDATE` / `Starting...`, then a live percentage —
   **don't power off the device mid-update.** It reboots on its own
   when done.

Requires your computer and the device to be on the same LAN (mDNS
discovery doesn't cross subnets/VLANs or most guest WiFi isolation).
Good for active development, at home/office.

### 8b. Pull-based updates from GitHub (works from anywhere)

Once the device is deployed away from your dev network, use this
instead — push a release to GitHub, the device notices and updates
itself over its normal internet connection, no shared LAN needed.

**One-time repo setup:**
1. In your GitHub repo, create a `firmware/` folder with two files:
   - `firmware/version.txt` — just the version number, e.g. `1.3.0`
   - `firmware/firmware.bin` — the compiled binary
2. To get `firmware.bin`: Sketch → Export Compiled Binary in the
   Arduino IDE, then commit that `.bin` alongside `version.txt`.
3. Decide public vs private repo (see security note below), then set
   in `Config.h`:
   - `GITHUB_VERSION_URL` and `GITHUB_FIRMWARE_URL` → the
     `raw.githubusercontent.com/<owner>/<repo>/<branch>/firmware/...`
     URLs for those two files
   - `GITHUB_USE_PRIVATE_REPO` → `true` (recommended) or `false`
   - `GITHUB_PAT` → a **fine-grained personal access token**, scoped
     to read-only "Contents" access on just this one repo (GitHub →
     Settings → Developer settings → Fine-grained tokens). Only
     needed if using a private repo.

**Every time you want to ship an update:**
1. Bump `FIRMWARE_VERSION` in `Config.h`.
2. Sketch → Export Compiled Binary.
3. Commit + push the new `firmware.bin` and matching `version.txt`
   (containing the same new version string) to the repo.
4. The device checks once a day automatically (or type `checkupdate`
   in Serial to force it now). If a new version is found, it prints
   the version and waits for you to type `update` — unless you've set
   `GITHUB_OTA_AUTO_INSTALL true`, in which case it flashes itself
   immediately without asking.
5. LCD shows the same `OTA UPDATE` / progress screen as the LAN path,
   then reboots on success.

**⚠️ Security note:** the compiled `firmware.bin` has
`SYNC_SHARED_SECRET` and `OTA_PASSWORD` baked into it as plain
strings — anyone who downloads that file can pull them out with
`strings firmware.bin`. A **public** repo means anyone can download
it. Use a **private** repo (the default, `GITHUB_USE_PRIVATE_REPO
true`) unless you're comfortable with that exposure — and if you ever
do publish a `.bin` publicly by mistake, rotate `SYNC_SHARED_SECRET`
in both `Config.h` and `Code.gs`.

Serial commands for this: `checkupdate` (check now), `update`
(install a version `checkupdate` already found), `status` (also shows
whether an update is pending).

## Power notes (battery-only, no charging circuit)

Running continuously (WiFi + RC522 + LCD backlight + RTC): roughly
**100–150mA average draw**. On a 10,000mAh power bank (accounting for
boost-conversion losses), that's very roughly **2 days of continuous
runtime** per charge — 20,000mAh roughly doubles that. Swap/recharge
as needed; no onboard charge management since none was wanted.

**Worth testing first:** some power banks auto-shut-off under light
load. Our draw stays comfortably above the usual ~50mA cutoff most
banks use, but cheap/older banks vary — leave it running on the
actual bank you plan to deploy for an hour or two before trusting it
in the field.

## Known limitations (out of scope for what you asked)

- No buzzer/LED scan-feedback beyond the LCD and status LED — easy to
  add later on the unused A0 pin.
- Single device, as specified. The Sheet already has a `Device`
  column, so a second unit later just needs a different `DEVICE_ID`.
- OTA over the IDE's Network Port (8a) needs the same LAN as your
  computer — use the GitHub pull method (8b) for updates once the
  device is deployed elsewhere.
- GitHub OTA firmware download assumes a normal (non-chunked) HTTPS
  response with a Content-Length header, which is what
  raw.githubusercontent.com serves — this won't work if you point
  `GITHUB_FIRMWARE_URL` at something that serves chunked responses.
