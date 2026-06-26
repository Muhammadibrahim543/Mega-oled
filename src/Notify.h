#ifndef NOTIFY_H
#define NOTIFY_H

// ════════════════════════════════════════════════════════════════
//  Notify.h — New-message notification system
//
//  When a new WT message is fully received:
//    1. OLED switches to full-screen NOTIFY page (drawPageNotify)
//    2. Passive buzzer beeps (if BUZZER_PIN >= 0)
//    3. Notification persists until any button press
//
//  notifyNewMessage() is called from ESPNow.cpp on PKT_DONE.
//  notifyDismiss()    is called from UI.cpp on any button event.
//  notifyTick()       is called from loop() for buzzer sequencing.
// ════════════════════════════════════════════════════════════════

#include "Config.h"
#include <cstdint>

// ── State ─────────────────────────────────────────────────────
extern bool    notifyActive;       // true while notification showing
extern char    notifyFile[48];     // path of the new file
extern uint32_t notifyReceivedMs;  // millis() when received
extern uint8_t notifyAnimFrame;    // animation counter for display

// ── API ───────────────────────────────────────────────────────
void notifyInit();                      // setup buzzer pin
void notifyNewMessage(const char* path);  // trigger notification
void notifyDismiss();                   // clear notification
void notifyTick();                      // manage buzzer + animation

#endif // NOTIFY_H
