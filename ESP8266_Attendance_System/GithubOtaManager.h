#ifndef GITHUB_OTA_MANAGER_H
#define GITHUB_OTA_MANAGER_H

#include <Arduino.h>
#include "Config.h"
#include "UIManager.h"

// Tier 6: pull-based OTA from a GitHub repo. Unlike OTAManager
// (ArduinoOTA, LAN-only, developer pushes from the IDE), this lets the
// device fetch updates from anywhere it has internet — no shared
// network with your computer needed. Checks version.txt periodically;
// downloads + flashes firmware.bin only when the version differs, and
// only actually installs automatically if GITHUB_OTA_AUTO_INSTALL is
// set (otherwise it just notifies and waits for the "update" command).
class GithubOtaManager {
public:
    GithubOtaManager(UIManager &ui) : _ui(ui) {}

    void begin();
    void tick(bool wifiConnected); // call every loop; runs the periodic check

    void checkNow();   // fetch version.txt, compare, notify (or auto-install)
    void installNow(); // download + flash firmware.bin right now

    bool updateAvailable() const { return _updateAvailable; }
    const String &availableVersion() const { return _availableVersion; }

private:
    UIManager &_ui;
    bool _wifiConnected = false;
    bool _updateAvailable = false;
    String _availableVersion;
    uint32_t _lastCheckMs = 0;

    bool _fetchLatestVersion(String &outVersion);
    void _performUpdate();
};

#endif // GITHUB_OTA_MANAGER_H
