#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include "Config.h"
#include "UIManager.h"

// Tier 6: firmware updates over WiFi via the Arduino IDE's "Network
// Port" upload path (ArduinoOTA). Only becomes active once WiFi is
// actually connected — pointless (and unreachable) during the setup
// portal or while offline.
//
// NOTE on blocking: once an update actually starts transferring,
// ArduinoOTA's internals block the loop until it finishes or fails —
// this is standard, expected behavior for OTA on ESP8266, not a bug.
// A firmware update is a deliberate maintenance action, not something
// that needs to run alongside RFID scanning.
class OTAManager {
public:
    OTAManager(UIManager &ui) : _ui(ui) {}

    void begin(); // call once in setup(); registers callbacks (doesn't start listening yet)
    void tick(bool wifiConnected); // call every loop

private:
    UIManager &_ui;
    bool _started = false;
};

#endif // OTA_MANAGER_H
