// ════════════════════════════════════════════════════════════════
//  LiveRadioDisplay.cpp — OLED draw for PAGE_LIVE_RADIO
//  SSD1306 128×64  |  Walkie-Talkie Receiver (ESP32)
//
//  Layout (128×64 pixels):
//
//  ┌─────────────────────────────┐  y=0
//  │  [●TX] / [◉RX] / [  ]IDLE  │  y=0..9   — state badge row
//  │  ─────────────────────────  │  y=10
//  │  CH 0 · ALPHA               │  y=12..23  — channel row
//  │  ─────────────────────────  │  y=24
//  │  [======       ] TX bar     │  y=26..33  — activity row
//  │   or  ●●●●● wave dots       │
//  │   or  "No signal"           │
//  ├─────────────────────────────┤  y=44
//  │  [<]CH  [PTT]  [>]CH        │  y=46..55  — button hint bar
//  │  Hold PTT to talk           │  y=56..63
//  └─────────────────────────────┘  y=63
//
//  Buttons (receiver hardware):
//    BTN_PREV short  → channel down
//    BTN_NEXT short  → channel up
//    BTN_PREV long   → PTT (arm = transmit, release = stop)
//    BTN_PREV_LONG / BTN_SEL_LONG → exit
// ════════════════════════════════════════════════════════════════

#if 0 // Disabled: drawPageLiveRadio is defined in Display.cpp with the unified futuristic HUD
#include "LiveRadioDisplay.h"
#include "Display.h"      // for extern oled, dispStr helpers
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <string.h>
#include <math.h>

// oled object is defined in Display.cpp — use it directly
extern Adafruit_SSD1306 oled;

// ── Channel callsign table (short, fits 128px) ───────────────
static const char* const _CH_NAMES[LR_CHANNEL_COUNT] = {
    "ALPHA", "BRAVO", "CHARLIE", "DELTA"
};

// ════════════════════════════════════════════════════════════════
//  DRAW HELPERS
// ════════════════════════════════════════════════════════════════

// Draw a small filled circle (3px radius) — used as status dot
static void _dot(int x, int y, bool fill) {
    if (fill) oled.fillCircle(x, y, 3, SSD1306_WHITE);
    else      oled.drawCircle(x, y, 3, SSD1306_WHITE);
}

// Draw a simple horizontal bar (progress / TX timer)
static void _hbar(int x, int y, int w, int h,
                  int fillW, bool invertFill) {
    oled.drawRect(x, y, w, h, SSD1306_WHITE);
    if (fillW > 0) {
        int fw = (fillW > w - 2) ? (w - 2) : fillW;
        if (invertFill) {
            // Filled = white block
            oled.fillRect(x + 1, y + 1, fw, h - 2, SSD1306_WHITE);
        } else {
            oled.fillRect(x + 1, y + 1, fw, h - 2, SSD1306_WHITE);
        }
    }
}

// Draw small signal-strength indicator (3 bars, x/y = bottom-left)
static void _sigBars(int x, int y, int level) {
    // level 0-3 = number of lit bars
    int heights[3] = {3, 5, 7};
    int bw = 3, gap = 2;
    for (int i = 0; i < 3; i++) {
        int bx = x + i * (bw + gap);
        int bh = heights[i];
        int by = y - bh;
        if (i < level)
            oled.fillRect(bx, by, bw, bh, SSD1306_WHITE);
        else
            oled.drawRect(bx, by, bw, bh, SSD1306_WHITE);
    }
}

// Draw animated wave / receiving dots (5 dots, one lit per frame)
static void _waveDots(int cx, int y, uint32_t ms) {
    int   n        = 5;
    int   spacing  = 10;
    int   startX   = cx - (n - 1) * spacing / 2;
    int   lit      = (int)((ms / 100) % n);
    for (int i = 0; i < n; i++) {
        int r = (i == lit) ? 3 : 2;
        if (i == lit)
            oled.fillCircle(startX + i * spacing, y, r, SSD1306_WHITE);
        else
            oled.drawCircle(startX + i * spacing, y, r, SSD1306_WHITE);
    }
}

// ════════════════════════════════════════════════════════════════
//  MAIN DRAW FUNCTION
// ════════════════════════════════════════════════════════════════

void drawPageLiveRadio(uint8_t  state,
                       uint8_t  channel,
                       uint32_t elapsedMs,
                       uint32_t packetCount,
                       int16_t  lastSample,
                       uint32_t ms)
{
    (void)packetCount;
    (void)lastSample;

    // ── ROW 0-9: STATE BADGE ─────────────────────────────────
    // Left side: state icon + label
    // Right side: signal bars (idle) or blinking dot (active)

    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    if (state == LR_TX) {
        // Blinking filled circle = transmitting
        bool blink = ((ms / 350) % 2);
        if (blink) oled.fillCircle(4, 5, 4, SSD1306_WHITE);
        else        oled.drawCircle(4, 5, 4, SSD1306_WHITE);

        // "TX" bold-ish label
        oled.setCursor(11, 1);
        oled.print("TRANSMITTING");

    } else if (state == LR_RX) {
        // Hollow circle with inner dot = receiving
        oled.drawCircle(4, 5, 4, SSD1306_WHITE);
        oled.fillCircle(4, 5, 2, SSD1306_WHITE);

        oled.setCursor(11, 1);
        oled.print("RECEIVING   ");

    } else {
        // Idle: just "LIVE RADIO" label + signal bars right
        oled.setCursor(2, 1);
        oled.print("LIVE RADIO");
        _sigBars(110, 9, 2);
    }

    // ── DIVIDER LINE ─────────────────────────────────────────
    oled.drawFastHLine(0, 11, 128, SSD1306_WHITE);

    // ── ROW 12-23: CHANNEL INFO ──────────────────────────────
    // "CH X  CALLSIGN"
    oled.setTextSize(1);

    // "CH" prefix dim, number bold (simulate with extra char)
    oled.setCursor(2, 13);
    oled.print("CH");

    // Channel number slightly larger using size 2
    oled.setTextSize(2);
    char chStr[3];
    snprintf(chStr, sizeof(chStr), "%d", channel);
    oled.setCursor(16, 11);
    oled.print(chStr);

    // Channel name — right aligned approx
    oled.setTextSize(1);
    const char* name = _CH_NAMES[channel < LR_CHANNEL_COUNT ? channel : 0];
    // Calculate right-align x (each char = 6px wide at size 1)
    int nameLen = strlen(name);
    int nameX   = 128 - nameLen * 6 - 2;
    if (nameX < 36) nameX = 36;
    oled.setCursor(nameX, 16);
    oled.print(name);

    // ── DIVIDER LINE ─────────────────────────────────────────
    oled.drawFastHLine(0, 26, 128, SSD1306_WHITE);

    // ── ROW 28-43: ACTIVITY AREA ─────────────────────────────

    if (state == LR_TX) {
        // TX: time bar showing elapsed vs max (8s)
        uint32_t maxMs  = 8000;
        int      barW   = 100;
        int      fillW  = (int)((float)elapsedMs / maxMs * barW);
        if (fillW > barW) fillW = barW;

        oled.setTextSize(1);
        oled.setCursor(2, 28);
        oled.print("TX");

        _hbar(14, 28, barW, 7, fillW, true);

        // Remaining seconds
        uint32_t remSec = elapsedMs < maxMs ? (maxMs - elapsedMs) / 1000 : 0;
        char tbuf[8];
        snprintf(tbuf, sizeof(tbuf), "%lus", (unsigned long)remSec);
        oled.setCursor(116, 28);
        oled.print(tbuf);

        // "HOLD [<] TO TALK" instruction
        oled.setCursor(10, 37);
        oled.print("PTT: hold [PREV]");

    } else if (state == LR_RX) {
        // RX: animated wave dots centered
        _waveDots(64, 35, ms);

        oled.setTextSize(1);
        oled.setCursor(30, 28);
        oled.print("RECEIVING...");

    } else {
        // IDLE: last heard info
        oled.setTextSize(1);

        // "STANDBY" centered top
        oled.setCursor(38, 28);
        oled.print("STANDBY");

        // Last heard time bottom
        uint32_t lrLast = lrLastPacketMs;
        if (lrLast > 0 && ms >= lrLast) {
            uint32_t ago = (ms - lrLast) / 1000;
            char buf[22];
            if (ago < 60)
                snprintf(buf, sizeof(buf), "Heard: %lus ago", (unsigned long)ago);
            else
                snprintf(buf, sizeof(buf), "Heard: %lum ago", (unsigned long)(ago / 60));
            oled.setCursor(2, 37);
            oled.print(buf);
        } else {
            oled.setCursor(14, 37);
            oled.print("No signal heard");
        }
    }

    // ── DIVIDER LINE ─────────────────────────────────────────
    oled.drawFastHLine(0, 46, 128, SSD1306_WHITE);

    // ── ROW 48-63: BUTTON HINT BAR ───────────────────────────
    // Three columns:  [<] CH  |  [PTT]  |  [>] CH
    //                 hold PREV = PTT   exit = hold SEL

    oled.setTextSize(1);

    // Left: [<] = CH down
    oled.setCursor(2, 48);
    oled.print("[<]");
    oled.setCursor(2, 57);
    oled.print("CH-");

    // Center: PTT
    oled.setCursor(44, 48);
    oled.print("[PTT]");
    oled.setCursor(40, 57);
    oled.print("HoldPREV");

    // Right: [>] = CH up
    oled.setCursor(104, 48);
    oled.print("[>]");
    oled.setCursor(104, 57);
    oled.print("CH+");

    // Thin vertical separators
    oled.drawFastVLine(40,  47, 17, SSD1306_WHITE);
    oled.drawFastVLine(100, 47, 17, SSD1306_WHITE);
}
#endif
