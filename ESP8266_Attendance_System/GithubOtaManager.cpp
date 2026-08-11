#include "GithubOtaManager.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Updater.h>

void GithubOtaManager::begin() {
    // Stagger the first check to ~60s after boot rather than immediately,
    // so it doesn't compete with the WiFi-connect / NTP-sync burst of
    // network activity right at startup. Subsequent checks are spaced
    // by the full interval from there.
    _lastCheckMs = millis() - GITHUB_OTA_CHECK_INTERVAL_MS + 60000UL;
}

void GithubOtaManager::tick(bool wifiConnected) {
    _wifiConnected = wifiConnected;
    if (!wifiConnected) return;

    uint32_t now = millis();
    if (now - _lastCheckMs >= GITHUB_OTA_CHECK_INTERVAL_MS) {
        _lastCheckMs = now;
        checkNow();
    }
}

bool GithubOtaManager::_fetchLatestVersion(String &outVersion) {
    WiFiClientSecure client;
    client.setInsecure(); // same pragmatic tradeoff as the Sheets sync — see SyncManager

    HTTPClient https;
    https.setTimeout(SYNC_HTTP_TIMEOUT_MS);
    https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (!https.begin(client, GITHUB_VERSION_URL)) return false;

    if (GITHUB_USE_PRIVATE_REPO) {
        https.addHeader("Authorization", "token " GITHUB_PAT);
    }

    int code = https.GET();
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        outVersion = https.getString();
        outVersion.trim();
        ok = outVersion.length() > 0;
    } else {
        Serial.print(F("[OTA-GH] version.txt fetch failed, HTTP "));
        Serial.println(code);
    }
    https.end();
    return ok;
}

void GithubOtaManager::triggerUpdateFromButton() {
    if (!_wifiConnected) {
        _ui.showTransientMessage("WiFi Disconnected", "Cannot update", 2000);
        Serial.println(F("[OTA-GH] Button update failed: WiFi not connected."));
        return;
    }

    _ui.showTransientMessage("Checking OTA...", "Please wait...", 2000);
    Serial.println(F("[OTA-GH] Button triggered OTA update check..."));

    String latest;
    if (!_fetchLatestVersion(latest)) {
        _ui.showTransientMessage("OTA Check Failed", "Check internet", 2000);
        Serial.println(F("[OTA-GH] Failed to fetch version.txt from GitHub."));
        return;
    }

    Serial.print(F("[OTA-GH] Running: v"));
    Serial.print(FIRMWARE_VERSION);
    Serial.print(F(" | Latest on GitHub: v"));
    Serial.println(latest);

    if (latest != String(FIRMWARE_VERSION)) {
        _updateAvailable = true;
        _availableVersion = latest;
        _ui.showTransientMessage("New v" + latest + " found!", "Updating now...", 2000);
        delay(1000);
        installNow();
    } else {
        _updateAvailable = false;
        _ui.showTransientMessage("v" + String(FIRMWARE_VERSION), "Up to date!", 2000);
        Serial.println(F("[OTA-GH] Device is already running the latest firmware version."));
    }
}

void GithubOtaManager::checkNow() {
    if (!_wifiConnected) {
        Serial.println(F("[OTA-GH] Not connected, can't check for updates."));
        return;
    }

    String latest;
    if (!_fetchLatestVersion(latest)) {
        Serial.println(F("[OTA-GH] Could not reach version.txt — check GITHUB_VERSION_URL / PAT / repo visibility."));
        return;
    }

    Serial.print(F("[OTA-GH] Running: "));
    Serial.print(FIRMWARE_VERSION);
    Serial.print(F("  Latest: "));
    Serial.println(latest);

    if (latest != String(FIRMWARE_VERSION)) {
        _updateAvailable = true;
        _availableVersion = latest;
        if (GITHUB_OTA_AUTO_INSTALL) {
            Serial.println(F("[OTA-GH] New version found, auto-installing (GITHUB_OTA_AUTO_INSTALL=true)..."));
            installNow();
        } else {
            Serial.println(F("[OTA-GH] New version available. Press OTA button or type 'update' to install."));
        }
    } else {
        _updateAvailable = false;
        Serial.println(F("[OTA-GH] Already up to date."));
    }
}

void GithubOtaManager::installNow() {
    if (!_wifiConnected) {
        Serial.println(F("[OTA-GH] Not connected, can't install."));
        return;
    }
    _performUpdate();
}

void GithubOtaManager::_performUpdate() {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    https.setTimeout(SYNC_HTTP_TIMEOUT_MS);
    https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (!https.begin(client, GITHUB_FIRMWARE_URL)) {
        Serial.println(F("[OTA-GH] Failed to start firmware download."));
        return;
    }
    if (GITHUB_USE_PRIVATE_REPO) {
        https.addHeader("Authorization", "token " GITHUB_PAT);
    }

    int httpCode = https.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.print(F("[OTA-GH] Firmware download failed, HTTP "));
        Serial.println(httpCode);
        https.end();
        return;
    }

    int contentLength = https.getSize();
    if (contentLength <= 0) {
        Serial.println(F("[OTA-GH] Missing/invalid Content-Length, aborting (this flow needs a non-chunked response)."));
        https.end();
        return;
    }

    if (!Update.begin(contentLength)) {
        Serial.println(F("[OTA-GH] Not enough free space for this update."));
        https.end();
        return;
    }

    _ui.showOtaStart();
    Serial.print(F("[OTA-GH] Downloading and flashing "));
    Serial.print(contentLength);
    Serial.println(F(" bytes..."));

    WiFiClient *stream = https.getStreamPtr();
    uint8_t buf[512];
    int written = 0;
    uint32_t lastUiUpdateMs = 0;

    while (https.connected() && written < contentLength) {
        size_t avail = stream->available();
        if (avail > 0) {
            size_t toRead = min(avail, sizeof(buf));
            size_t readBytes = stream->readBytes(buf, toRead);
            size_t writtenNow = Update.write(buf, readBytes);
            written += writtenNow;

            if (writtenNow != readBytes) {
                Serial.println(F("[OTA-GH] Flash write mismatch, aborting."));
                break;
            }

            uint32_t now = millis();
            if (now - lastUiUpdateMs > 300) { // throttle LCD writes during the download
                lastUiUpdateMs = now;
                _ui.showOtaProgress((written * 100) / contentLength);
            }
        }
        yield(); // feed the watchdog / let the WiFi stack breathe during this tight loop
    }
    https.end();

    bool ok = (written == contentLength) && Update.end();

    if (!ok) {
        Serial.print(F("[OTA-GH] Update failed. Bytes written: "));
        Serial.print(written);
        Serial.print(F("/"));
        Serial.print(contentLength);
        Serial.print(F("  Updater error: "));
        Serial.println(Update.getError());
        _ui.showOtaEnd(false);
        return;
    }

    Serial.println(F("[OTA-GH] Update successful, rebooting..."));
    _ui.showOtaEnd(true);
    delay(500);
    ESP.restart();
}
