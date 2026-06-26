#ifndef LIVE_RADIO_DISPLAY_H
#define LIVE_RADIO_DISPLAY_H

// ════════════════════════════════════════════════════════════════
//  LiveRadioDisplay.h — OLED draw function for Live Radio page
//  SSD1306 128×64  |  Walkie-Talkie Receiver (ESP32)
//
//  Provides drawPageLiveRadio() called from UI.cpp uiTick().
//  All other drawPage* functions live in Display.cpp/.h.
// ════════════════════════════════════════════════════════════════

#include "Config.h"
#include "LiveRadio.h"
#include <cstdint>

// Draw the Live Radio page on the SSD1306 OLED.
//   state        — LR_IDLE / LR_TX / LR_RX
//   channel      — 0-3
//   elapsedMs    — TX duration or RX duration (ms)
//   packetCount  — TX or RX packet count for this session
//   lastSample   — last PCM sample (for VU meter, may be 0)
//   ms           — millis() at draw time (for animations)
void drawPageLiveRadio(uint8_t  state,
                       uint8_t  channel,
                       uint32_t elapsedMs,
                       uint32_t packetCount,
                       int16_t  lastSample,
                       uint32_t ms);

#endif // LIVE_RADIO_DISPLAY_H
