#include "NetworkManager.h"

void NetworkManager::begin() {
    _apName = String(DEVICE_ID) + "-Setup";

    _wm.setConfigPortalBlocking(false); // critical: keeps our main loop non-blocking
    _wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
    _wm.setHostname(DEVICE_ID);
    _wm.setAPCallback([this](WiFiManager *wm) {
        (void)wm;
        _portalActive = true; // fires once the hotspot actually opens
    });

    // First tries any previously-saved credentials (one bounded blocking
    // attempt at boot, same as the DS3231/RC522 self-tests already do —
    // acceptable one-time startup cost). If that fails, it opens the
    // setup hotspot and returns immediately without waiting for anyone
    // to configure it.
    bool connectedImmediately;
    if (strlen(WIFI_SETUP_AP_PASSWORD) > 0) {
        connectedImmediately = _wm.autoConnect(_apName.c_str(), WIFI_SETUP_AP_PASSWORD);
    } else {
        connectedImmediately = _wm.autoConnect(_apName.c_str());
    }

    _state = connectedImmediately ? State::CONNECTED : State::CONFIG_PORTAL;
}

void NetworkManager::tick() {
    _wm.process(); // services the captive portal's DNS/web server if active

    if (WiFi.status() == WL_CONNECTED) {
        _state = State::CONNECTED;
        _portalActive = false;
        return;
    }

    if (_portalActive) {
        if (_wm.getConfigPortalActive()) {
            _state = State::CONFIG_PORTAL;
        } else {
            // Portal was open but timed out (WIFI_PORTAL_TIMEOUT_S) without
            // anyone completing setup. Reboot for a clean retry rather than
            // hand-rolling a re-arm of WiFiManager's internal state.
            ESP.restart();
        }
        return;
    }

    // Not connected, portal not active: a real drop after normal operation
    // (e.g. router reboot). The ESP8266 WiFi stack usually reconnects on
    // its own; this is just a periodic safety-net nudge for when it doesn't.
    _state = State::DISCONNECTED;
    uint32_t now = millis();
    if (now - _lastReconnectAttemptMs >= WIFI_RECONNECT_RETRY_MS) {
        _lastReconnectAttemptMs = now;
        WiFi.reconnect();
    }
}

void NetworkManager::resetWifiSettings() {
    _wm.resetSettings();
    delay(100);
    ESP.restart();
}
