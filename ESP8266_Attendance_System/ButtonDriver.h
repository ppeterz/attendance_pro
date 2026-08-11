#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <Arduino.h>
#include "Config.h"

enum class ButtonEvent : uint8_t { NONE, SHORT_PRESS, OTA_PRESS, LONG_PRESS, VERY_LONG_PRESS };

// Debounced gesture state machine for the single mode button:
//   1 short press (tap <500ms)     -> SHORT_PRESS (Manual sync)
//   hold <1.5s (500ms-1800ms)      -> OTA_PRESS (GitHub OTA Update)
//   hold >=2s                      -> LONG_PRESS (Card Enrollment)
//   hold >=6s                      -> VERY_LONG_PRESS (WiFi Reset)
class ButtonDriver {
public:
    void begin();
    ButtonEvent tick();

private:
    enum class State { IDLE, DEBOUNCING, PRESSED, LONG_FIRED, VERY_LONG_FIRED };
    State _state = State::IDLE;
    uint32_t _stateChangedAt = 0;  // debounce timing
    uint32_t _pressStartMs = 0;    // confirmed press-start timestamp
    bool _rawPressed();
};

#endif // BUTTON_DRIVER_H
