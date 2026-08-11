#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <Arduino.h>
#include "Config.h"

enum class ButtonEvent : uint8_t { NONE, SHORT_PRESS, LONG_PRESS, VERY_LONG_PRESS };

// Debounced 3-gesture state machine for the single mode button:
//   short press        -> SHORT_PRESS
//   held >=1.5s        -> LONG_PRESS (fires once, while still held)
//   held >=10s         -> VERY_LONG_PRESS (fires once, while still held)
// Non-blocking — call tick() every loop iteration; it returns an event
// exactly once per threshold crossed during a single physical press.
class ButtonDriver {
public:
    void begin();
    ButtonEvent tick();

private:
    enum class State { IDLE, DEBOUNCING, PRESSED, LONG_FIRED, VERY_LONG_FIRED };
    State _state = State::IDLE;
    uint32_t _stateChangedAt = 0;  // debounce timing
    uint32_t _pressStartMs = 0;    // confirmed press-start, used for both thresholds
    bool _rawPressed();
};

#endif // BUTTON_DRIVER_H
