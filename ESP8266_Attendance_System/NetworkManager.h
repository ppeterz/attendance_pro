#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFiManager.h>
#include "Config.h"

// Tier 6: WiFi connectivity via WiFiManager's non-blocking captive
// portal. No SSID/password is ever hardcoded (Directive 9) — first
// boot (or after resetWifiSettings()) opens a phone-connectable
// hotspot with a setup page; WiFiManager persists whatever's entered
// there via the ESP8266 SDK's own WiFi storage, and reconnects to it
// automatically on every future boot.
class NetworkManager {
public:
    enum class State { CONFIG_PORTAL, CONNECTING, CONNECTED, DISCONNECTED };

    void begin();
    void tick(); // non-blocking, call every loop — services the portal + reconnect retries
    State state() const { return _state; }
    bool isConnected() const { return _state == State::CONNECTED; }

    // The AP name shown to the phone during setup (e.g. "ATTEND-01-Setup").
    // Valid once begin() has run.
    const String &setupApName() const { return _apName; }

    // Wipes saved WiFi credentials and reboots straight into the setup
    // portal. Wire this to the button's VERY_LONG_PRESS event, or the
    // "resetwifi" Serial CLI command.
    void resetWifiSettings();

private:
    WiFiManager _wm;
    State _state = State::CONNECTING;
    String _apName;
    bool _portalActive = false;      // set true by WiFiManager's AP callback
    uint32_t _lastReconnectAttemptMs = 0;
};

#endif // NETWORK_MANAGER_H
