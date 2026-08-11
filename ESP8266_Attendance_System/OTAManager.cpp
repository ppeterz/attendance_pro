#include "OTAManager.h"
#include <ArduinoOTA.h>

void OTAManager::begin() {
    ArduinoOTA.setHostname(DEVICE_ID);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([this]() {
        Serial.println(F("[OTA] Update starting..."));
        _ui.showOtaStart();
    });

    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        int pct = (total > 0) ? (int)((progress * 100UL) / total) : 0;
        _ui.showOtaProgress(pct);
    });

    ArduinoOTA.onEnd([this]() {
        Serial.println(F("[OTA] Update complete, rebooting..."));
        _ui.showOtaEnd(true);
    });

    ArduinoOTA.onError([this](ota_error_t error) {
        Serial.print(F("[OTA] Error: "));
        Serial.println((int)error);
        _ui.showOtaEnd(false);
    });
}

void OTAManager::tick(bool wifiConnected) {
    if (wifiConnected && !_started) {
        ArduinoOTA.begin();
        _started = true;
        Serial.println(F("[OTA] Ready — device discoverable for network uploads."));
    }
    if (_started) {
        ArduinoOTA.handle();
    }
}
