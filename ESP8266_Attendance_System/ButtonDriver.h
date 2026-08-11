#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <Arduino.h>
#include "Config.h"

enum class ButtonEvent : uint8_t { NONE, SHORT_PRESS, DOUBLE_PRESS, LONG_PRESS, OTA_PRESS, VERY_LONG_PRESS };

// Debounced gesture state machine for the single mode button:
//   short press        -> SHORT_PRESS
//   double press       -> DOUBLE_PRESS (2 quick taps)
//   held >=1.5s        -> LONG_PRESS (Enrollment mode)
//   held >=4.0s        -> OTA_PRESS (GitHub OTA update)
//   held >=10s         -> VERY_LONG_PRESS (WiFi reset)
class ButtonDriver {
public:
    void begin();
    ButtonEvent tick();

private:
    enum class State { IDLE, DEBOUNCING, PRESSED, LONG_FIRED, OTA_FIRED, VERY_LONG_FIRED };
    State _state = State::IDLE;
    uint32_t _stateChangedAt = 0;  // debounce timing
    uint32_t _pressStartMs = 0;    // confirmed press-start, used for thresholds
    uint32_t _lastReleaseMs = 0;   // used for double-click detection
    bool _rawPressed();
};

#endif // BUTTON_DRIVER_H
