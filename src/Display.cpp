// ════════════════════════════════════════════════════════════════
//  Display.cpp — SSD1306 128×64 rendering + animation engine
//  Walkie-Talkie Receiver  (ESP32)
//  UI v2.0 — Futuristic / Tactical HUD aesthetic
// ════════════════════════════════════════════════════════════════

#include "Display.h"
#include "SDCard.h"
#include <Wire.h>
#include <math.h>

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);

static uint32_t _lastFlushMs = 0;

// ════════════════════════════════════════════════════════════════
//  LIFECYCLE
// ════════════════════════════════════════════════════════════════
bool dispInit() {
    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(400000);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println("[DISP] SSD1306 init FAILED");
        return false;
    }
    oled.setTextWrap(false);
    oled.clearDisplay();
    oled.display();
    Serial.println("[DISP] SSD1306 OK");
    return true;
}

void dispClear() { oled.clearDisplay(); }

void dispFlush() { oled.display(); }

void dispTick() {
    uint32_t now = millis();
    if (now - _lastFlushMs >= DISPLAY_TICK_MS) {
        _lastFlushMs = now;
        dispFlush();
    }
}

// ════════════════════════════════════════════════════════════════
//  PRIMITIVES
// ════════════════════════════════════════════════════════════════
void dispStr(int x, int y, const char* s, uint8_t size) {
    oled.setTextSize(size);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(x, y);
    oled.print(s);
}

void dispStrC(int y, const char* s, uint8_t size) {
    oled.setTextSize(size);
    int16_t bx, by; uint16_t bw, bh;
    oled.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
    int x = (OLED_W - bw) / 2;
    if (x < 0) x = 0;
    dispStr(x, y, s, size);
}

void dispStrR(int y, const char* s, uint8_t size) {
    oled.setTextSize(size);
    int16_t bx, by; uint16_t bw, bh;
    oled.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
    dispStr(OLED_W - bw - 1, y, s, size);
}

void dispLine(int x0, int y0, int x1, int y1) {
    oled.drawLine(x0, y0, x1, y1, SSD1306_WHITE);
}

void dispRect(int x, int y, int w, int h, bool fill) {
    if (fill) oled.fillRect(x, y, w, h, SSD1306_WHITE);
    else       oled.drawRect(x, y, w, h, SSD1306_WHITE);
}

void dispRoundRect(int x, int y, int w, int h, int r, bool fill) {
    if (fill) oled.fillRoundRect(x, y, w, h, r, SSD1306_WHITE);
    else       oled.drawRoundRect(x, y, w, h, r, SSD1306_WHITE);
}

void dispCircle(int x, int y, int r, bool fill) {
    if (fill) oled.fillCircle(x, y, r, SSD1306_WHITE);
    else       oled.drawCircle(x, y, r, SSD1306_WHITE);
}

void dispPixel(int x, int y, uint8_t col) {
    oled.drawPixel(x, y, col ? SSD1306_WHITE : SSD1306_BLACK);
}

void dispHLine(int x, int y, int w) {
    oled.drawFastHLine(x, y, w, SSD1306_WHITE);
}

void dispVLine(int x, int y, int h) {
    oled.drawFastVLine(x, y, h, SSD1306_WHITE);
}

void dispProgressBar(int x, int y, int w, int h, uint8_t pct) {
    oled.drawRect(x, y, w, h, SSD1306_WHITE);
    int fill = ((int)(w - 2) * pct) / 100;
    if (fill > 0) oled.fillRect(x + 1, y + 1, fill, h - 2, SSD1306_WHITE);
}

// ════════════════════════════════════════════════════════════════
//  ANIMATIONS
// ════════════════════════════════════════════════════════════════

void animPulseRing(int cx, int cy, int r, uint32_t phase) {
    float t = (phase % 1200) / 1200.0f;
    int   rr = r + (int)(t * 14.0f);
    uint8_t alpha = (uint8_t)(255 * (1.0f - t));
    if (alpha > 60) oled.drawCircle(cx, cy, rr, SSD1306_WHITE);
    oled.drawCircle(cx, cy, r, SSD1306_WHITE);
}

void animWaveform(int x, int y, int w, int h,
                  const float* samples, int n) {
    if (n == 0) return;
    int barW = max(1, w / n);
    for (int i = 0; i < n && (x + i * barW) < (x + w); i++) {
        int bh = (int)(samples[i] * h);
        if (bh < 1) bh = 1;
        if (bh > h) bh = h;
        int by = y + h - bh;
        oled.fillRect(x + i * barW, by, barW - 1, bh, SSD1306_WHITE);
    }
}

void animSpinner(int cx, int cy, int r, uint32_t phase) {
    int seg = (phase / 80) % 8;
    for (int i = 0; i < 8; i++) {
        float angle = i * (3.14159f / 4.0f);
        int x1 = cx + (int)((r - 3) * cosf(angle));
        int y1 = cy + (int)((r - 3) * sinf(angle));
        int x2 = cx + (int)(r * cosf(angle));
        int y2 = cy + (int)(r * sinf(angle));
        if (i == seg || i == (seg + 1) % 8 || i == (seg + 7) % 8) {
            oled.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
        }
    }
}

void animDots(int x, int y, uint32_t phase) {
    for (int i = 0; i < 3; i++) {
        int offset = (int)(sinf((phase / 250.0f) + i * 1.05f) * 3.0f);
        oled.fillCircle(x + i * 10, y - offset, 2, SSD1306_WHITE);
    }
}

void animStatic(int x, int y, int w, int h, uint32_t seed) {
    uint32_t s = seed ^ 0xDEADBEEF;
    for (int i = 0; i < 60; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        int px = x + (int)(s & 0x7F) % w;
        int py = y + (int)((s >> 8) & 0x3F) % h;
        oled.drawPixel(px, py, SSD1306_WHITE);
    }
}

// ════════════════════════════════════════════════════════════════
//  FUTURISTIC HUD HELPERS
// ════════════════════════════════════════════════════════════════

static void drawCornerBrackets(int x, int y, int w, int h, int len) {
    oled.drawFastHLine(x, y, len, SSD1306_WHITE);
    oled.drawFastVLine(x, y, len, SSD1306_WHITE);
    oled.drawFastHLine(x + w - len, y, len, SSD1306_WHITE);
    oled.drawFastVLine(x + w - 1, y, len, SSD1306_WHITE);
    oled.drawFastHLine(x, y + h - 1, len, SSD1306_WHITE);
    oled.drawFastVLine(x, y + h - len, len, SSD1306_WHITE);
    oled.drawFastHLine(x + w - len, y + h - 1, len, SSD1306_WHITE);
    oled.drawFastVLine(x + w - 1, y + h - len, len, SSD1306_WHITE);
}

static void drawSegBar(int x, int y, int w, int h, uint8_t pct) {
    const int segs = 12;
    int segW = (w - segs) / segs;
    int filled = (segs * pct) / 100;
    for (int i = 0; i < segs; i++) {
        int sx = x + i * (segW + 1);
        if (i < filled) oled.fillRect(sx, y, segW, h, SSD1306_WHITE);
        else            oled.drawRect(sx, y, segW, h, SSD1306_WHITE);
    }
}

static void drawHUDHeader(const char* title) {
    oled.drawFastHLine(0, 0, OLED_W, SSD1306_WHITE);
    oled.fillRect(0, 1, OLED_W, 9, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setTextSize(1);
    int16_t bx, by; uint16_t bw, bh;
    oled.getTextBounds(title, 0, 0, &bx, &by, &bw, &bh);
    oled.setCursor((OLED_W - bw) / 2, 2);
    oled.print(title);
    oled.setTextColor(SSD1306_WHITE);
    oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);
}

static void drawHUDFooter(const char* l, const char* m, const char* r) {
    oled.drawFastHLine(0, OLED_H - 11, OLED_W, SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    if (l && l[0] != '\0') {
        oled.drawRect(0, OLED_H - 10, 37, 10, SSD1306_WHITE);
        int16_t bx, by; uint16_t bw, bh;
        oled.getTextBounds(l, 0, 0, &bx, &by, &bw, &bh);
        oled.setCursor((37 - bw) / 2, OLED_H - 8);
        oled.print(l);
    }

    if (m && m[0] != '\0') {
        oled.fillRect(38, OLED_H - 10, 52, 10, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        int16_t bx, by; uint16_t bw, bh;
        oled.getTextBounds(m, 0, 0, &bx, &by, &bw, &bh);
        oled.setCursor(38 + (52 - bw) / 2, OLED_H - 8);
        oled.print(m);
        oled.setTextColor(SSD1306_WHITE);
    }

    if (r && r[0] != '\0') {
        oled.drawRect(91, OLED_H - 10, 37, 10, SSD1306_WHITE);
        int16_t bx, by; uint16_t bw, bh;
        oled.getTextBounds(r, 0, 0, &bx, &by, &bw, &bh);
        oled.setCursor(91 + (37 - bw) / 2, OLED_H - 8);
        oled.print(r);
    }
}

static void animRadarSweep(int cx, int cy, int r, uint32_t phase) {
    float angle = (phase % 2000) / 2000.0f * 2.0f * 3.14159f;
    int x2 = cx + (int)(r * cosf(angle));
    int y2 = cy + (int)(r * sinf(angle));
    oled.drawLine(cx, cy, x2, y2, SSD1306_WHITE);
    for (int t = 1; t <= 3; t++) {
        float a2 = angle - t * 0.18f;
        int tx = cx + (int)((r - t * 2) * cosf(a2));
        int ty = cy + (int)((r - t * 2) * sinf(a2));
        oled.drawPixel(tx, ty, SSD1306_WHITE);
    }
}

static void drawSignalBars(int x, int y, bool active) {
    for (int i = 0; i < 3; i++) {
        int bh = 2 + i * 2;
        int bx = x + i * 4;
        int by = y + (6 - bh);
        if (active || i == 0) oled.fillRect(bx, by, 3, bh, SSD1306_WHITE);
        else                  oled.drawRect(bx, by, 3, bh, SSD1306_WHITE);
    }
}

// ════════════════════════════════════════════════════════════════
//  PAGE: SPLASH SCREEN — Professional tactical boot sequence
//  Phase 0 (0–400ms)   : scanline wipe reveals solid white border
//  Phase 1 (400–900ms) : "WALKIE" types letter by letter
//  Phase 2 (900–1400ms): "TALKIE RX" types in, divider appears
//  Phase 3 (1400–2000ms): boot-check lines scroll + progress bar
// ════════════════════════════════════════════════════════════════
void drawPageSplash(uint32_t ms) {
    oled.clearDisplay();

    const int BW = 2;   // border weight in pixels

    // ── Phase 0: scanline wipe (0..400 ms) ──────────────────────
    if (ms < 400) {
        // A bright horizontal scan-bar sweeps top-to-bottom
        int scanY = (int)((float)ms / 400.0f * (OLED_H - 4));
        if (scanY > OLED_H - 4) scanY = OLED_H - 4;

        // draw everything revealed so far in solid white
        oled.fillRect(0, 0, OLED_W, scanY, SSD1306_WHITE);

        // bright leading edge
        oled.fillRect(0, scanY, OLED_W, 4, SSD1306_WHITE);
        return;
    }

    // After phase 0, draw the full outer border
    // Top / Bottom bars
    oled.fillRect(0, 0, OLED_W, BW, SSD1306_WHITE);
    oled.fillRect(0, OLED_H - BW, OLED_W, BW, SSD1306_WHITE);
    // Left / Right bars
    oled.fillRect(0, 0, BW, OLED_H, SSD1306_WHITE);
    oled.fillRect(OLED_W - BW, 0, BW, OLED_H, SSD1306_WHITE);

    // Corner cross-hatch accents (4 px outward notch at each corner)
    oled.fillRect(0, 0, 8, BW, SSD1306_BLACK);
    oled.fillRect(OLED_W - 8, 0, 8, BW, SSD1306_BLACK);
    oled.fillRect(0, OLED_H - BW, 8, BW, SSD1306_BLACK);
    oled.fillRect(OLED_W - 8, OLED_H - BW, 8, BW, SSD1306_BLACK);
    oled.fillRect(0, 0, BW, 8, SSD1306_BLACK);
    oled.fillRect(OLED_W - BW, 0, BW, 8, SSD1306_BLACK);
    oled.fillRect(0, OLED_H - 8, BW, 8, SSD1306_BLACK);
    oled.fillRect(OLED_W - BW, OLED_H - 8, BW, 8, SSD1306_BLACK);
    // Corner L-brackets
    oled.fillRect(0,        0,        6, BW, SSD1306_WHITE);
    oled.fillRect(0,        0,        BW, 6, SSD1306_WHITE);
    oled.fillRect(OLED_W-6, 0,        6, BW, SSD1306_WHITE);
    oled.fillRect(OLED_W-BW,0,        BW, 6, SSD1306_WHITE);
    oled.fillRect(0,        OLED_H-BW, 6, BW, SSD1306_WHITE);
    oled.fillRect(0,        OLED_H-6,  BW, 6, SSD1306_WHITE);
    oled.fillRect(OLED_W-6, OLED_H-BW, 6, BW, SSD1306_WHITE);
    oled.fillRect(OLED_W-BW,OLED_H-6,  BW, 6, SSD1306_WHITE);

    // ── Phase 1: type "WALKIE" (400..900 ms) ────────────────────
    {
        const char* title1 = "WALKIE";
        int nChars = strlen(title1);
        int visible = (ms < 400) ? 0 : (int)((float)(ms - 400) / 500.0f * nChars);
        if (visible > nChars) visible = nChars;

        oled.setTextSize(2);
        oled.setTextColor(SSD1306_WHITE);

        // Centre the full word, then print only 'visible' chars
        int16_t bx, by; uint16_t bw2, bh;
        oled.getTextBounds(title1, 0, 0, &bx, &by, &bw2, &bh);
        int startX = (OLED_W - bw2) / 2;

        char buf[16] = {0};
        strncpy(buf, title1, visible);
        buf[visible] = '\0';
        oled.setCursor(startX, 8);
        oled.print(buf);

        // blinking cursor while typing
        if (visible < nChars && (ms / 100) % 2 == 0) {
            int16_t bx2, by2; uint16_t bw3, bh2;
            oled.getTextBounds(buf, 0, 0, &bx2, &by2, &bw3, &bh2);
            oled.fillRect(startX + bw3, 8, 2, 14, SSD1306_WHITE);
        }
        oled.setTextSize(1);
    }

    // ── Phase 2: type "TALKIE  RX" (900..1400 ms) ────────────────
    if (ms >= 900) {
        const char* title2 = "TALKIE  RX";
        int nChars = strlen(title2);
        int visible = (int)((float)(ms - 900) / 500.0f * nChars);
        if (visible > nChars) visible = nChars;

        oled.setTextSize(2);

        int16_t bx, by; uint16_t bw2, bh;
        oled.getTextBounds(title2, 0, 0, &bx, &by, &bw2, &bh);
        int startX = (OLED_W - bw2) / 2;

        char buf[16] = {0};
        strncpy(buf, title2, visible);
        buf[visible] = '\0';
        oled.setCursor(startX, 24);
        oled.print(buf);

        if (visible < nChars && (ms / 100) % 2 == 0) {
            int16_t bx2, by2; uint16_t bw3, bh2;
            oled.getTextBounds(buf, 0, 0, &bx2, &by2, &bw3, &bh2);
            oled.fillRect(startX + bw3, 24, 2, 14, SSD1306_WHITE);
        }
        oled.setTextSize(1);

        // Thin divider below the title block
        if (ms >= 1100) {
            oled.drawFastHLine(10, 42, OLED_W - 20, SSD1306_WHITE);
        }
    }

    // ── Phase 3: boot-check text + progress bar (1400..2000 ms) ─
    if (ms >= 1400) {
        float prog = (float)(ms - 1400) / 600.0f;  // 0.0 → 1.0
        if (prog > 1.0f) prog = 1.0f;

        // Scrolling checklist (3 items appear sequentially)
        const char* checks[] = {"SYS OK", "NET OK", "READY "};
        int numShown = (int)(prog * 3) + 1;
        if (numShown > 3) numShown = 3;

        for (int i = 0; i < numShown; i++) {
            oled.setCursor(10, 45 + i * 7);
            oled.print("[");
            oled.print((char)0x18); // checkmark substitute
            oled.print("] ");
            oled.print(checks[i]);
        }

        // Progress bar
        int barX = 10, barY = OLED_H - BW - 6, barW2 = OLED_W - 20, barH = 4;
        oled.drawRect(barX, barY, barW2, barH, SSD1306_WHITE);
        int filled = (int)(prog * (barW2 - 2));
        if (filled > 0)
            oled.fillRect(barX + 1, barY + 1, filled, barH - 2, SSD1306_WHITE);

        // Percentage right-aligned
        char pct[6];
        snprintf(pct, sizeof(pct), "%d%%", (int)(prog * 100));
        int16_t bx, by; uint16_t bw2, bh;
        oled.getTextBounds(pct, 0, 0, &bx, &by, &bw2, &bh);
        oled.setCursor(OLED_W - 10 - bw2, 45);
        oled.print(pct);
    }

    oled.display();
}


// ════════════════════════════════════════════════════════════════
//  PAGE: MODE SELECT — Boot-time menu
// ════════════════════════════════════════════════════════════════
void drawPageModeSelect(uint8_t selIdx, float scrollY, uint32_t ms) {
    oled.clearDisplay();
    
    const char* modes[5] = {"File Transfer", "Walkie-Talkie", "Live Radio", "File Browser", "Settings"};
    const int MENU_Y = 12;
    const int MENU_H = OLED_H - MENU_Y - 11;
    const int ITEM_H = 18;
    
    for (int i = 0; i < 5; i++) {
        int py = MENU_Y + (int)((i - scrollY) * ITEM_H) + 3;
        
        if (py + ITEM_H <= MENU_Y) continue;
        if (py >= MENU_Y + MENU_H) continue;
        
        if (i == selIdx) {
            oled.fillRect(4, py, OLED_W - 8, 12, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
            bool blink = (ms / 300) % 2;
            if (blink) {
                oled.setCursor(6, py + 2); oled.print(">");
                oled.setCursor(OLED_W - 12, py + 2); oled.print("<");
            }
        } else {
            oled.setTextColor(SSD1306_WHITE);
        }
        
        if (py + 2 >= MENU_Y && py + 10 < MENU_Y + MENU_H) {
            int16_t bx, by; uint16_t bw, bh;
            oled.getTextBounds(modes[i], 0, 0, &bx, &by, &bw, &bh);
            int cx = (OLED_W - bw) / 2;
            if (cx < 0) cx = 0;
            
            oled.setCursor(cx, py + 2);
            oled.print(modes[i]);
        }
    }
    
    oled.setTextColor(SSD1306_WHITE);
    oled.fillRect(0, 0, OLED_W, MENU_Y, SSD1306_BLACK);
    drawHUDHeader("SELECT MODE");
    oled.fillRect(0, MENU_Y + MENU_H, OLED_W, 11, SSD1306_BLACK);
    drawHUDFooter("UP", "SELECT", "DOWN");
}

// ════════════════════════════════════════════════════════════════
//  PAGE: FILE TRANSFER
// ════════════════════════════════════════════════════════════════
void drawPageFileTransfer(uint32_t ms) {
    oled.clearDisplay();
    drawHUDHeader("FILE TRANSFER");
    drawCornerBrackets(0, 11, OLED_W, OLED_H - 22, 5);
    
    int cx = OLED_W / 2;
    int cy = 32;
    animPulseRing(cx, cy, 12, ms);
    oled.fillCircle(cx, cy, 3, SSD1306_WHITE);
    
    bool flash = (ms / 500) % 2;
    if (flash) dispStrC(45, "WAITING FOR DATA", 1);
    
    drawHUDFooter("<MENU", "", "");
}

// ════════════════════════════════════════════════════════════════
//  PAGE: SETTINGS
// ════════════════════════════════════════════════════════════════
#include "Settings.h"
void drawPageSettings(uint8_t selIdx, uint32_t ms) {
    oled.clearDisplay();
    drawHUDHeader("SETTINGS");
    
    const char* labels[3] = {"Volume", "Mic Gain", "Screen Saver"};
    int values[3] = {setVolume, setMicGain, setScreenSav};
    
    int yOff = 16;
    for (int i = 0; i < 3; i++) {
        int py = yOff + i * 14;
        
        if (i == selIdx) {
            oled.fillRect(2, py - 1, OLED_W - 4, 12, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else {
            oled.setTextColor(SSD1306_WHITE);
        }
        
        oled.setCursor(6, py + 1);
        oled.print(labels[i]);
        
        oled.setCursor(OLED_W - 35, py + 1);
        if (i == 0 || i == 1) {
            oled.printf("%d", values[i]);
        } else {
            if (values[i] == 0) oled.print("OFF");
            else if (values[i] == 1) oled.print("1m");
            else oled.print("5m");
        }
    }
    
    oled.setTextColor(SSD1306_WHITE);
    drawHUDFooter("-", "SAVE", "+");
}

// ════════════════════════════════════════════════════════════════
//  PAGE: FILE BROWSER
// ════════════════════════════════════════════════════════════════
void drawPageFileBrowser(uint32_t ms) {
    oled.clearDisplay();
    drawHUDHeader("FILE BROWSER");
    dispStrC(30, "Coming soon...", 1);
    drawHUDFooter("<MENU", "", "");
}

// ════════════════════════════════════════════════════════════════
//  PAGE: STANDBY — Tactical radar HUD
// ════════════════════════════════════════════════════════════════
void drawPageStandby(uint8_t unread, bool peerSeen, uint32_t ms) {
    oled.clearDisplay();
    drawHUDHeader("WALKIE-RX");
    drawCornerBrackets(0, 0, OLED_W, OLED_H, 5);

    // Radar circle (left panel)
    int cx = 30, cy = 33, r = 16;
    oled.drawCircle(cx, cy, r, SSD1306_WHITE);
    oled.drawCircle(cx, cy, r / 2, SSD1306_WHITE);
    oled.drawFastHLine(cx - r, cy, r * 2, SSD1306_WHITE);
    oled.drawFastVLine(cx, cy - r, r * 2, SSD1306_WHITE);
    animRadarSweep(cx, cy, r - 1, ms);
    oled.fillCircle(cx, cy, 2, SSD1306_WHITE);

    // Divider
    oled.drawFastVLine(50, 12, 37, SSD1306_WHITE);

    // Right panel: status
    dispStr(55, 13, "PEER:", 1);
    if (peerSeen) {
        dispStr(90, 13, "LINK", 1);
        drawSignalBars(116, 13, true);
    } else {
        bool blink = (ms / 500) % 2;
        if (blink) dispStr(90, 13, "----", 1);
        drawSignalBars(116, 13, false);
    }

    // Unread messages badge
    if (unread > 0) {
        char ubuf[8];
        snprintf(ubuf, sizeof(ubuf), "MSG:%d", unread);
        bool flash = (ms / 400) % 2;
        if (flash) {
            oled.fillRect(54, 27, 70, 10, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
            oled.setTextSize(1);
            int16_t bx, by; uint16_t bw, bh;
            oled.getTextBounds(ubuf, 0, 0, &bx, &by, &bw, &bh);
            oled.setCursor(54 + (70 - bw) / 2, 29);
            oled.print(ubuf);
            oled.setTextColor(SSD1306_WHITE);
        } else {
            oled.drawRect(54, 27, 70, 10, SSD1306_WHITE);
            dispStrC(29, ubuf, 1);
        }
    } else {
        dispStr(55, 27, "STANDBY", 1);
    }

    // Signal activity
    dispStr(55, 41, "SIG:", 1);
    animDots(82, 46, ms);

    drawHUDFooter("<INBOX", "PTT", "INBOX>");
}

// ════════════════════════════════════════════════════════════════
//  PAGE: RECEIVING — Data transfer HUD
// ════════════════════════════════════════════════════════════════
void drawPageReceiving(const char* fname, uint8_t pct,
                       uint32_t elapsed, uint32_t bytesRx, uint32_t bytesTotal) {
    oled.clearDisplay();
    drawHUDHeader("// RECEIVING //");

    char fshort[20];
    const char* bn = strrchr(fname, '/');
    bn = bn ? bn + 1 : fname;
    strncpy(fshort, bn, 18);
    fshort[18] = '\0';
    char* dot = strrchr(fshort, '.');
    if (dot) *dot = '\0';
    dispStrC(13, fshort, 1);

    animStatic(106, 2, 20, 7, millis());

    drawSegBar(4, 24, 120, 6, pct);

    char pbuf[8];
    snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
    dispStr(2, 33, pbuf, 1);

    char bbuf[24];
    if (bytesTotal > 0) snprintf(bbuf, sizeof(bbuf), "%luB/%luB", bytesRx, bytesTotal);
    else                snprintf(bbuf, sizeof(bbuf), "%luB", bytesRx);
    dispStrR(33, bbuf, 1);

    char ebuf[16];
    uint32_t sec = elapsed / 1000;
    snprintf(ebuf, sizeof(ebuf), "T+%02lu:%02lu", sec / 60, sec % 60);
    dispStrC(33, ebuf, 1);

    animSpinner(8, 47, 7, millis());

    for (int i = 0; i < 5; i++) {
        bool on = ((millis() / 120 + i) % 5) < 2;
        if (on) oled.fillRect(20 + i * 6, 44, 4, 4, SSD1306_WHITE);
        else    oled.drawRect(20 + i * 6, 44, 4, 4, SSD1306_WHITE);
    }

    drawHUDFooter("", "ABORT", "");
}

// ════════════════════════════════════════════════════════════════
//  PAGE: INBOX — Butter-smooth pixel-scroll list
//  scrollY : fractional visual position (item units, 0 = newest)
//  sel     : integer target (for highlight logic)
//  Newest message = sdFiles[count-1], shown at logical index 0.
// ════════════════════════════════════════════════════════════════
void drawPageInbox(uint8_t count, uint8_t sel, float scrollY,
                   bool playing, uint32_t playPosMs, uint32_t playTotalMs) {

    // ── Layout constants ──────────────────────────────────────
    const int IB_LIST_Y  = 11;          // y where list area starts (below header)
    const int IB_LIST_H  = OLED_H - IB_LIST_Y - 11; // height of scrollable area (above footer)
    const int IB_ROW_H   = 11;          // pixels per row
    const int IB_ROWS    = IB_LIST_H / IB_ROW_H; // visible rows (≈ 4)
    const int IB_SB_X    = OLED_W - 4;  // scrollbar x

    // ── Empty state ───────────────────────────────────────────
    if (count == 0) {
        drawHUDHeader("[ INBOX ]");
        drawCornerBrackets(10, IB_LIST_Y + 4, OLED_W - 20, IB_LIST_H - 8, 4);
        dispStrC(IB_LIST_Y + IB_LIST_H / 2 - 8, "NO MESSAGES", 1);
        dispStrC(IB_LIST_Y + IB_LIST_H / 2 + 2, "INBOX CLEAR", 1);
        drawHUDFooter("<BACK", "BACK", "");
        return;
    }

    // ── Header ────────────────────────────────────────────────
    char ibuf[16];
    snprintf(ibuf, sizeof(ibuf), "[ INBOX: %02d ]", count);
    drawHUDHeader(ibuf);

    // ── Pixel offset from fractional scrollY ──────────────────
    float pixelOffset = scrollY * IB_ROW_H;   // sub-pixel smooth offset

    // ── Clip region: draw only inside list area ────────────────
    int firstRow = (int)scrollY;            // topmost visible logical index
    if (firstRow < 0) firstRow = 0;

    for (int r = firstRow - 1; r <= firstRow + IB_ROWS + 1; r++) {
        if (r < 0 || r >= count) continue;

        // Pixel y for this row in the scrolled coordinate space
        int ry_raw = IB_LIST_Y + (int)((r - scrollY) * IB_ROW_H);

        // Skip rows completely outside the list area
        if (ry_raw + IB_ROW_H <= IB_LIST_Y) continue;
        if (ry_raw >= IB_LIST_Y + IB_LIST_H)  continue;

        // Reverse map: logical index 0 → sdFiles[count-1] (newest)
        int fi = (count - 1) - r;
        const char* fullpath = sdFiles[fi];
        const char* bn = strrchr(fullpath, '/');
        bn = bn ? bn + 1 : fullpath;
        char namebuf[18];
        strncpy(namebuf, bn, 17);
        namebuf[17] = '\0';
        char* dot2 = strrchr(namebuf, '.');
        if (dot2) *dot2 = '\0';

        bool isNewest  = (r == 0);
        bool isSelected = (r == (int)sel);

        if (isSelected) {
            // ── Selected row: inverted fill ──────────────────
            int fillY = ry_raw;
            int fillH = IB_ROW_H - 1;
            if (fillY < IB_LIST_Y) { fillH -= (IB_LIST_Y - fillY); fillY = IB_LIST_Y; }
            if (fillY + fillH > IB_LIST_Y + IB_LIST_H) fillH = IB_LIST_Y + IB_LIST_H - fillY;
            if (fillH > 0) {
                oled.fillRect(0, fillY, IB_SB_X - 1, fillH, SSD1306_WHITE);
            }

            int textY = ry_raw + 2;
            if (textY >= IB_LIST_Y && textY < IB_LIST_Y + IB_LIST_H - 1) {
                oled.setTextColor(SSD1306_BLACK);
                oled.setTextSize(1);
                if (playing) {
                    bool blink = (millis() / 300) % 2;
                    oled.setCursor(1, textY);
                    oled.print(blink ? ">>" : " >");
                    oled.setCursor(15, textY);
                } else {
                    oled.setCursor(1, textY);
                    oled.print(isNewest ? "\x07" : "\xBB");  // bullet or »
                    oled.setCursor(9, textY);
                }
                oled.print(namebuf);
                oled.setTextColor(SSD1306_WHITE);
            }

        } else {
            // ── Non-selected row ─────────────────────────────
            int textY = ry_raw + 2;
            if (textY >= IB_LIST_Y && textY < IB_LIST_Y + IB_LIST_H - 1) {
                oled.setTextColor(SSD1306_WHITE);
                oled.setTextSize(1);
                if (isNewest) {
                    oled.setCursor(1, textY);
                    oled.print("*");
                    oled.setCursor(9, textY);
                } else {
                    oled.setCursor(5, textY);
                }
                oled.print(namebuf);
            }

            int sepY = ry_raw + IB_ROW_H - 1;
            if (sepY > IB_LIST_Y && sepY < IB_LIST_Y + IB_LIST_H) {
                oled.drawFastHLine(5, sepY, IB_SB_X - 8, SSD1306_WHITE);
            }
        }
    }

    // ── Hard clip: black out anything above/below list area ───
    oled.fillRect(0, 0,        OLED_W, IB_LIST_Y,              SSD1306_BLACK);
    oled.fillRect(0, IB_LIST_Y + IB_LIST_H, OLED_W, OLED_H - IB_LIST_Y - IB_LIST_H, SSD1306_BLACK);

    // Redraw header on top of any bleed
    drawHUDHeader(ibuf);

    // ── Scrollbar (right edge, proportional thumb) ────────────
    if (count > IB_ROWS) {
        int trackH = IB_LIST_H;
        int thumbH = max(4, trackH * IB_ROWS / count);
        float maxScroll = (float)(count - IB_ROWS);
        float frac = (maxScroll > 0) ? (scrollY / maxScroll) : 0.0f;
        int thumbY = IB_LIST_Y + (int)(frac * (trackH - thumbH));
        oled.drawFastVLine(IB_SB_X + 1, IB_LIST_Y, trackH, SSD1306_WHITE);
        oled.fillRect(IB_SB_X, thumbY, 3, thumbH, SSD1306_WHITE);
    }

    // ── Playback progress bar (thin segmented, above footer) ──
    if (playing && playTotalMs > 0) {
        uint8_t pp = (uint8_t)((uint64_t)playPosMs * 100 / playTotalMs);
        drawSegBar(0, IB_LIST_Y + IB_LIST_H - 3, IB_SB_X - 1, 3, pp);
    }

    // ── Footer ────────────────────────────────────────────────
    if (playing) drawHUDFooter("<PREV", "STOP",  "NEXT>");
    else         drawHUDFooter("<PREV", "PLAY",  "NEXT>");
}

// ════════════════════════════════════════════════════════════════
//  PAGE: RECORDING — Audio capture HUD
// ════════════════════════════════════════════════════════════════
void drawPageRecording(uint32_t elapsed, uint32_t recBytes, float rms) {
    oled.clearDisplay();
    drawHUDHeader("// REC //");
    drawCornerBrackets(2, 12, OLED_W - 4, OLED_H - 24, 5);

    bool blink = (millis() / 400) % 2;
    if (blink) {
        oled.fillCircle(12, 30, 5, SSD1306_WHITE);
    } else {
        oled.drawCircle(12, 30, 5, SSD1306_WHITE);
        oled.fillCircle(12, 30, 2, SSD1306_WHITE);
    }

    oled.setTextSize(2);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(22, 25);
    oled.print("REC");

    char tbuf[12];
    uint32_t sec = elapsed / 1000;
    snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu", sec / 60, sec % 60);
    dispStrR(27, tbuf, 1);

    int lvlPct = (int)(rms / 200.0f * 100.0f);
    if (lvlPct > 100) lvlPct = 100;
    drawSegBar(8, 42, 112, 5, lvlPct);

    dispStr(8, 50, "LVL", 1);

    char bbuf[16];
    if (recBytes < 1024) snprintf(bbuf, sizeof(bbuf), "%luB", recBytes);
    else                 snprintf(bbuf, sizeof(bbuf), "%luK", recBytes / 1024);
    dispStrR(50, bbuf, 1);

    drawHUDFooter("", "STOP", "");
}

// ════════════════════════════════════════════════════════════════
//  PAGE: SENDING (outgoing WT message)
// ════════════════════════════════════════════════════════════════
void drawPageSending(const char* fname, uint8_t pct, uint8_t enwState) {
    oled.clearDisplay();
    drawHUDHeader("// SENDING //");

    const char* bn = strrchr(fname, '/');
    bn = bn ? bn + 1 : fname;
    char fshort[18];
    strncpy(fshort, bn, 17); fshort[17] = '\0';
    char* dot = strrchr(fshort, '.');
    if (dot) *dot = '\0';
    dispStrC(13, fshort, 1);

    const char* stlbl = (enwState == 0) ? "[ IDLE ]"   :
                        (enwState == 1) ? "[ SCAN.. ]" :
                        (enwState == 2) ? "[ TX >> ]"  :
                        (enwState == 3) ? "[ DONE ]"   : "[ ERROR ]";
    dispStrC(24, stlbl, 1);

    drawSegBar(4, 34, 120, 6, pct);

    char pbuf[8];
    snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
    dispStrC(43, pbuf, 1);

    animSpinner(OLED_W - 10, 38, 7, millis());

    if (enwState == 2) {
        for (int i = 0; i < 6; i++) {
            bool on = ((millis() / 100 + i) % 6) < 3;
            if (on) oled.fillRect(4 + i * 7, 43, 5, 3, SSD1306_WHITE);
        }
    }

    drawHUDFooter("", "ABORT", "");
}

// ════════════════════════════════════════════════════════════════
//  PAGE: NOTIFICATION (new message received)
// ════════════════════════════════════════════════════════════════
void drawPageNotify(const char* fname, uint32_t ageMs, uint8_t animFrame) {
    oled.clearDisplay();

    uint32_t ms = millis();
    bool flashOn = (ms / 200) % 2;

    if (flashOn) {
        oled.drawRect(0, 0, OLED_W, OLED_H, SSD1306_WHITE);
        oled.drawRect(2, 2, OLED_W - 4, OLED_H - 4, SSD1306_WHITE);
    } else {
        oled.drawRect(1, 1, OLED_W - 2, OLED_H - 2, SSD1306_WHITE);
    }

    int cx = OLED_W / 2;
    animPulseRing(cx, 18, 11, ms);
    oled.drawRect(cx - 9, 12, 18, 12, SSD1306_WHITE);
    oled.drawLine(cx - 9, 12, cx, 18, SSD1306_WHITE);
    oled.drawLine(cx + 9, 12, cx, 18, SSD1306_WHITE);

    if (flashOn) {
        oled.fillRect(20, 27, 88, 10, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        oled.setTextSize(1);
        int16_t bx, by; uint16_t bw, bh;
        oled.getTextBounds(">> NEW MESSAGE <<", 0, 0, &bx, &by, &bw, &bh);
        oled.setCursor(20 + (88 - bw) / 2, 29);
        oled.print(">> NEW MESSAGE <<");
        oled.setTextColor(SSD1306_WHITE);
    } else {
        dispStrC(29, ">> NEW MESSAGE <<", 1);
    }

    const char* bn = strrchr(fname, '/');
    bn = bn ? bn + 1 : fname;
    char fshort[18];
    strncpy(fshort, bn, 17); fshort[17] = '\0';
    char* dot = strrchr(fshort, '.');
    if (dot) *dot = '\0';
    dispStrC(39, fshort, 1);

    char abuf[16];
    uint32_t sec = ageMs / 1000;
    if (sec < 60) snprintf(abuf, sizeof(abuf), "%lus ago", sec);
    else          snprintf(abuf, sizeof(abuf), "%lum ago", sec / 60);
    dispStrC(50, abuf, 1);
}

// ════════════════════════════════════════════════════════════════
//  PAGE: LIVE RADIO — Real-time ESP-NOW Walkie-Talkie HUD
//  state      : LR_IDLE(0) / LR_TX(1) / LR_RX(2)
//  channel    : 0-3
//  elapsedMs  : ms since TX started (or ms since last RX packet)
//  pktCount   : packets sent (TX) or received (RX)
//  lastSample : last int16_t sample value (for VU bar)
//  ms         : millis() for animation phase
// ════════════════════════════════════════════════════════════════
void drawPageLiveRadio(uint8_t state, uint8_t channel,
                       uint32_t elapsedMs, uint32_t pktCount,
                       int16_t lastSample, uint32_t ms) {
    oled.clearDisplay();

    // ── Channel names ────────────────────────────────────────
    static const char* const CH_NAMES[] = {"ALPHA","BRAVO","CHARLIE","DELTA"};
    const char* chName = (channel < 4) ? CH_NAMES[channel] : "???";

    // ── Header (inverted bar with channel badge) ─────────────
    oled.fillRect(0, 0, OLED_W, 10, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setTextSize(1);

    // "LR" label left
    oled.setCursor(2, 2);
    oled.print("LR");

    // Channel name centered
    {
        char chbuf[12];
        snprintf(chbuf, sizeof(chbuf), "CH%d-%s", channel, chName);
        int16_t bx, by; uint16_t bw, bh;
        oled.getTextBounds(chbuf, 0, 0, &bx, &by, &bw, &bh);
        oled.setCursor((OLED_W - bw) / 2, 2);
        oled.print(chbuf);
    }

    // State label right  [IDLE] / [ TX ] / [ RX ]
    {
        const char* stLabel = (state == 1) ? "TX" : (state == 2) ? "RX" : "--";
        int16_t bx, by; uint16_t bw, bh;
        oled.getTextBounds(stLabel, 0, 0, &bx, &by, &bw, &bh);
        oled.setCursor(OLED_W - bw - 2, 2);
        oled.print(stLabel);
    }
    oled.setTextColor(SSD1306_WHITE);
    oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);

    // ── Center animation area (y 12..50) ─────────────────────
    int cx = OLED_W / 2;
    int cy = 31;

    if (state == 0) {
        // IDLE: radar sweep
        int r = 16;
        oled.drawCircle(cx, cy, r, SSD1306_WHITE);
        oled.drawCircle(cx, cy, r / 2, SSD1306_WHITE);
        oled.drawFastHLine(cx - r, cy, r * 2, SSD1306_WHITE);
        oled.drawFastVLine(cx, cy - r, r * 2, SSD1306_WHITE);
        // Radar sweep line
        float angle = (ms % 2000) / 2000.0f * 2.0f * 3.14159f;
        int x2 = cx + (int)((r - 1) * cosf(angle));
        int y2 = cy + (int)((r - 1) * sinf(angle));
        oled.drawLine(cx, cy, x2, y2, SSD1306_WHITE);
        oled.fillCircle(cx, cy, 2, SSD1306_WHITE);

        // "STANDBY" text
        dispStrC(51, "STANDBY", 1);

    } else if (state == 1) {
        // TX: pulsing mic icon + animated waveform bars

        // Mic icon (circle + stand)
        int mx = 24, my = 28;
        bool blink = (ms / 250) % 2;
        if (blink) oled.fillCircle(mx, my, 8, SSD1306_WHITE);
        else       oled.drawCircle(mx, my, 8, SSD1306_WHITE);
        oled.drawFastVLine(mx, my + 8, 5, SSD1306_WHITE);
        oled.drawFastHLine(mx - 5, my + 13, 11, SSD1306_WHITE);

        // VU bar (right half): sample -> bar height
        int vuPct = (int)(abs(lastSample) / 327);  // 32767 -> 100%
        if (vuPct > 100) vuPct = 100;
        int barMaxH = 22;
        int barH = (barMaxH * vuPct) / 100;
        if (barH < 2) barH = 2;

        // Animated bars (simulated waveform using phase + sample)
        for (int i = 0; i < 6; i++) {
            float ph = (ms / 80.0f) + i * 0.9f;
            int bh2 = 4 + (int)(fabsf(sinf(ph)) * barH);
            if (bh2 > barMaxH) bh2 = barMaxH;
            int bx2 = 72 + i * 8;
            int by2 = cy + barMaxH / 2 - bh2;
            oled.fillRect(bx2, by2, 5, bh2, SSD1306_WHITE);
        }

        // Elapsed time
        char tbuf[10];
        uint32_t sec = elapsedMs / 1000;
        snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu", sec / 60, sec % 60);
        dispStr(64, 51, tbuf, 1);

        // "PTT HOLD" blink
        if (blink) dispStr(4, 51, "* TX *", 1);

    } else {
        // RX: incoming waveform animation + pulse rings

        // Pulse rings from center
        animPulseRing(cx, cy, 8,  ms);
        animPulseRing(cx, cy, 14, ms + 400);

        // Antenna icon (top of pulse)
        oled.drawFastVLine(cx, cy - 18, 8, SSD1306_WHITE);
        oled.drawFastHLine(cx - 5, cy - 18, 11, SSD1306_WHITE);
        oled.fillCircle(cx, cy, 3, SSD1306_WHITE);

        // Packet counter (left)
        char pkbuf[12];
        snprintf(pkbuf, sizeof(pkbuf), "#%lu", pktCount);
        dispStr(4, 22, pkbuf, 1);

        // Elapsed since first packet
        char tbuf[10];
        uint32_t sec = elapsedMs / 1000;
        snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu", sec / 60, sec % 60);
        dispStr(4, 33, tbuf, 1);

        // Animated signal bars (right side)
        for (int i = 0; i < 3; i++) {
            int bh2 = 3 + i * 3;
            bool on = ((ms / 200 + i) % 3) != 0;
            int bx2 = OLED_W - 18 + i * 5;
            int by2 = 35 - bh2;
            if (on) oled.fillRect(bx2, by2, 4, bh2, SSD1306_WHITE);
            else    oled.drawRect(bx2, by2, 4, bh2, SSD1306_WHITE);
        }

        // "RECEIVING" blink
        bool blink2 = (ms / 350) % 2;
        if (blink2) {
            oled.fillRect(0, 50, OLED_W, 9, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
            dispStrC(52, ">> RECEIVING <<", 1);
            oled.setTextColor(SSD1306_WHITE);
        }
    }

    // ── Footer ───────────────────────────────────────────────
    oled.drawFastHLine(0, OLED_H - 11, OLED_W, SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    // Left: channel nav  |  Center: PTT  |  Right: channel nav
    oled.drawRect(0, OLED_H - 10, 30, 10, SSD1306_WHITE);
    oled.setCursor(4, OLED_H - 8);
    oled.print("<< CH");

    if (state != 1) {
        // Only show PTT hint when not already transmitting
        oled.fillRect(31, OLED_H - 10, 66, 10, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        int16_t bx3, by3; uint16_t bw3, bh3;
        oled.getTextBounds("[ PTT ]", 0, 0, &bx3, &by3, &bw3, &bh3);
        oled.setCursor(31 + (66 - bw3) / 2, OLED_H - 8);
        oled.print("[ PTT ]");
        oled.setTextColor(SSD1306_WHITE);
    }

    oled.drawRect(98, OLED_H - 10, 30, 10, SSD1306_WHITE);
    oled.setCursor(100, OLED_H - 8);
    oled.print("CH >>");
}