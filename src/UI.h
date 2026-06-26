#ifndef UI_H
#define UI_H

// ════════════════════════════════════════════════════════════════
//  UI.h — Application state machine  v2.0
//  Walkie-Talkie Receiver (ESP32)
//
//  Page hierarchy:
//
//    PAGE_STANDBY  ← root
//      ├── PAGE_INBOX → PAGE_RECORDING → PAGE_SENDING
//
//    Auto-triggered (from anywhere):
//      PAGE_RECEIVING  (ESP-NOW file transfer)
//      PAGE_NOTIFY     (new message alert)
//
//  3-button layout:
//    BTN_PREV = left     BTN_SEL = center (confirm)    BTN_NEXT = right
// ════════════════════════════════════════════════════════════════

#include "Config.h"
#include <cstdint>

// ── Page IDs ──────────────────────────────────────────────────
#define PAGE_MODE_SELECT 0   // Boot-time mode chooser (root of all)
#define PAGE_STANDBY     1   // WT standby (Walkie-Talkie mode root)
#define PAGE_RECEIVING   2   // WT incoming file
#define PAGE_INBOX       3   // WT message list
#define PAGE_RECORDING   4   // WT recording
#define PAGE_SENDING     5   // WT outgoing file
#define PAGE_NOTIFY      6   // New message alert
#define PAGE_LIVE_RADIO  7   // Live Radio mode root (real-time ESP-NOW voice)
#define PAGE_FILE_TRANSFER 8 // Dedicated file transfer mode
#define PAGE_SPLASH      9   // Boot animation screen
#define PAGE_SETTINGS    10  // Settings menu
#define PAGE_FILE_BROWSER 11 // Full File Manager

extern uint8_t uiPage;
extern uint8_t uiPrevPage;

void uiInit();
void uiTick();
void uiHandleBtn(uint8_t event);

#endif // UI_H