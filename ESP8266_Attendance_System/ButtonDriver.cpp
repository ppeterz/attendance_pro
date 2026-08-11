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
                _state = State::IDLE; // was noise, bail out
            } else if (now - _stateChangedAt >= BUTTON_DEBOUNCE_MS) {
                _state = State::PRESSED;
                _pressStartMs = now; // confirmed press-start time, used for both thresholds
            }
            break;

        case State::PRESSED:
            if (!pressed) {
                _state = State::IDLE;
                if (now - _lastReleaseMs < 400) {
                    _lastReleaseMs = 0;
                    return ButtonEvent::DOUBLE_PRESS;
                }
                _lastReleaseMs = now;
                return ButtonEvent::SHORT_PRESS;
            } else if (now - _pressStartMs >= BUTTON_VERY_LONG_PRESS_MS) {
                _state = State::VERY_LONG_FIRED;
                return ButtonEvent::VERY_LONG_PRESS;
            } else if (now - _pressStartMs >= BUTTON_OTA_PRESS_MS) {
                _state = State::OTA_FIRED;
                return ButtonEvent::OTA_PRESS;
            } else if (now - _pressStartMs >= BUTTON_LONG_PRESS_MS) {
                _state = State::LONG_FIRED;
                return ButtonEvent::LONG_PRESS;
            }
            break;

        case State::LONG_FIRED:
            if (!pressed) {
                _state = State::IDLE;
            } else if (now - _pressStartMs >= BUTTON_VERY_LONG_PRESS_MS) {
                _state = State::VERY_LONG_FIRED;
                return ButtonEvent::VERY_LONG_PRESS;
            } else if (now - _pressStartMs >= BUTTON_OTA_PRESS_MS) {
                _state = State::OTA_FIRED;
                return ButtonEvent::OTA_PRESS;
            }
            break;

        case State::OTA_FIRED:
            if (!pressed) {
                _state = State::IDLE;
            } else if (now - _pressStartMs >= BUTTON_VERY_LONG_PRESS_MS) {
                _state = State::VERY_LONG_FIRED;
                return ButtonEvent::VERY_LONG_PRESS;
            }
            break;

        case State::VERY_LONG_FIRED:
            if (!pressed) {
                _state = State::IDLE; // just wait out the release, already fired
            }
            break;
    }
    return ButtonEvent::NONE;
}
