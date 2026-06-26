// ════════════════════════════════════════════════════════════════
//  Buttons.cpp — Debounced 3-button input
//  Walkie-Talkie Receiver (ESP32)
// ════════════════════════════════════════════════════════════════

#include "Buttons.h"
#include <Arduino.h>

bool btnPrevHeld = false;
bool btnSelHeld  = false;
bool btnNextHeld = false;

struct BtnState {
    uint8_t  pin;
    bool     down;
    bool     longFired;
    bool     pttArmed;
    uint32_t downMs;
};

static BtnState _b[3];   // 0=PREV, 1=SEL, 2=NEXT

void btnsInit() {
    _b[0] = {BTN_PREV_PIN, false, false, false, 0};
    _b[1] = {BTN_SEL_PIN,  false, false, false, 0};
    _b[2] = {BTN_NEXT_PIN, false, false, false, 0};
    for (int i = 0; i < 3; i++) {
        pinMode(_b[i].pin, INPUT_PULLUP);
    }
}

uint8_t btnsTick() {
    uint32_t now = millis();
    const uint8_t shortEv[3] = { BTN_PREV_SHORT, BTN_SEL_SHORT, BTN_NEXT_SHORT };
    const uint8_t longEv[3]  = { BTN_PREV_LONG,  BTN_SEL_LONG,  BTN_NEXT_LONG  };

    static bool chordFired = false;
    bool prevDown = (digitalRead(_b[0].pin) == LOW);
    bool nextDown = (digitalRead(_b[2].pin) == LOW);

    if (prevDown && nextDown) {
        if (!chordFired) {
            chordFired = true;
            _b[0].longFired = true; // Prevent individual events
            _b[2].longFired = true;
            if (_b[0].pttArmed) {
                _b[0].pttArmed = false;
                return BTN_PTT_UP; // Release PTT if it was armed before we return menu event
            }
            return BTN_GLOBAL_MENU;
        }
    } else {
        if (!prevDown && !nextDown) chordFired = false;
    }

    for (int i = 0; i < 3; i++) {
        bool pressed = (digitalRead(_b[i].pin) == LOW);
        bool* heldPtr = (i == 0) ? &btnPrevHeld :
                        (i == 1) ? &btnSelHeld  : &btnNextHeld;

        if (pressed && !_b[i].down) {
            // Leading edge
            _b[i].down     = true;
            _b[i].downMs   = now;
            _b[i].longFired = false;
            _b[i].pttArmed  = false;
            *heldPtr = true;
        }

        if (pressed && _b[i].down && !_b[i].longFired) {
            uint32_t held = now - _b[i].downMs;
            if (held >= BTN_LONG_MS) {
                _b[i].longFired = true;
                // PREV long = PTT arm event
                if (i == 0) {
                    _b[i].pttArmed = true;
                    return BTN_PTT_DOWN;
                }
                return longEv[i];
            }
        }

        if (!pressed && _b[i].down) {
            // Trailing edge
            _b[i].down  = false;
            *heldPtr = false;
            if ((now - _b[i].downMs) < BTN_DEBOUNCE_MS) continue; // glitch
            if (_b[i].pttArmed) {
                _b[i].pttArmed = false;
                return BTN_PTT_UP;
            }
            if (!_b[i].longFired) {
                return shortEv[i];
            }
        }
    }
    return BTN_NONE;
}
