#ifndef BUTTONS_H
#define BUTTONS_H

// ════════════════════════════════════════════════════════════════
//  Buttons.h — 3-button input handler
//  Provides debounced short press, long press events.
//  BTN_PREV: navigate back / PTT (hold for recording)
//  BTN_SEL:  confirm / play / stop
//  BTN_NEXT: navigate forward
// ════════════════════════════════════════════════════════════════

#include "Config.h"
#include <cstdint>

// ── Event types ───────────────────────────────────────────────
#define BTN_NONE       0
#define BTN_PREV_SHORT 1
#define BTN_SEL_SHORT  2
#define BTN_NEXT_SHORT 3
#define BTN_PREV_LONG  4
#define BTN_SEL_LONG   5
#define BTN_NEXT_LONG  6
#define BTN_PTT_DOWN   7   // BTN_PREV held >= BTN_LONG_MS: PTT armed
#define BTN_PTT_UP     8   // BTN_PREV released while PTT was armed
#define BTN_GLOBAL_MENU 9  // PREV + NEXT pressed together

void  btnsInit();        // set up GPIO pins with pull-ups
uint8_t btnsTick();      // call from loop(); returns one BTN_* event or BTN_NONE

// ── Raw state (for hold-detection outside tick) ───────────────
extern bool btnPrevHeld;
extern bool btnSelHeld;
extern bool btnNextHeld;

#endif // BUTTONS_H
