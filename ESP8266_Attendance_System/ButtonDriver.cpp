#include "ButtonDriver.h"

void ButtonDriver::begin() {
    pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
    _state = State::IDLE;
}

bool ButtonDriver::_rawPressed() {
    return digitalRead(MODE_BUTTON_PIN) == LOW; // pull-up idle HIGH, pressed = LOW
}

ButtonEvent ButtonDriver::tick() {
    uint32_t now = millis();
    bool pressed = _rawPressed();

    switch (_state) {
        case State::IDLE:
            if (pressed) {
                _state = State::DEBOUNCING;
                _stateChangedAt = now;
            }
            break;

        case State::DEBOUNCING:
            if (!pressed) {
                _state = State::IDLE; // noise, return to idle
            } else if (now - _stateChangedAt >= BUTTON_DEBOUNCE_MS) {
                _state = State::PRESSED;
                _pressStartMs = now;
            }
            break;

        case State::PRESSED: {
            uint32_t duration = now - _pressStartMs;
            if (!pressed) {
                _state = State::IDLE;
                if (duration < BUTTON_SHORT_MAX_MS) {
                    return ButtonEvent::SHORT_PRESS;
                } else if (duration >= BUTTON_OTA_MIN_MS && duration <= BUTTON_OTA_MAX_MS) {
                    return ButtonEvent::OTA_PRESS;
                }
            } else if (duration >= BUTTON_VERY_LONG_PRESS_MS) {
                _state = State::VERY_LONG_FIRED;
                return ButtonEvent::VERY_LONG_PRESS;
            } else if (duration >= BUTTON_LONG_PRESS_MS) {
                _state = State::LONG_FIRED;
                return ButtonEvent::LONG_PRESS;
            }
            break;
        }

        case State::LONG_FIRED:
            if (!pressed) {
                _state = State::IDLE;
            } else if (now - _pressStartMs >= BUTTON_VERY_LONG_PRESS_MS) {
                _state = State::VERY_LONG_FIRED;
                return ButtonEvent::VERY_LONG_PRESS;
            }
            break;

        case State::VERY_LONG_FIRED:
            if (!pressed) {
                _state = State::IDLE;
            }
            break;
    }
    return ButtonEvent::NONE;
}
