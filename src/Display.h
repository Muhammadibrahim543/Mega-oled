#ifndef DISPLAY_H
#define DISPLAY_H

// ════════════════════════════════════════════════════════════════
//  Display.h — SSD1306 128×64 OLED driver + animation engine
//  v2.0: drawPageMenu() added
// ════════════════════════════════════════════════════════════════

#include "Config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

extern Adafruit_SSD1306 oled;

// ── Lifecycle ─────────────────────────────────────────────────
bool  dispInit();
void  dispFlush();
void  dispClear();
void  dispTick();

// ── Primitives ────────────────────────────────────────────────
void  dispStr(int x, int y, const char* s, uint8_t size = 1);
void  dispStrC(int y, const char* s, uint8_t size = 1);
void  dispStrR(int y, const char* s, uint8_t size = 1);
void  dispLine(int x0, int y0, int x1, int y1);
void  dispRect(int x, int y, int w, int h, bool fill = false);
void  dispRoundRect(int x, int y, int w, int h, int r, bool fill = false);
void  dispCircle(int x, int y, int r, bool fill = false);
void  dispPixel(int x, int y, uint8_t col = 1);
void  dispBitmap(int x, int y, const uint8_t* bmp, int w, int h);
void  dispHLine(int x, int y, int w);
void  dispVLine(int x, int y, int h);
void  dispProgressBar(int x, int y, int w, int h, uint8_t pct);

// ── Animation helpers ─────────────────────────────────────────
void  animPulseRing(int cx, int cy, int r, uint32_t phase);
void  animWaveform(int x, int y, int w, int h,
                   const float* samples, int n);
void  animSpinner(int cx, int cy, int r, uint32_t phase);
void  animDots(int x, int y, uint32_t phase);
void  animStatic(int x, int y, int w, int h, uint32_t seed);

// ── Page-level draw functions ─────────────────────────────────
void  drawPageSplash(uint32_t ms);
void  drawPageModeSelect(uint8_t selIdx, float scrollY, uint32_t ms);
void  drawPageFileTransfer(uint32_t ms);
void  drawPageSettings(uint8_t selIdx, uint32_t ms);
void  drawPageFileBrowser(uint32_t ms);
void  drawPageStandby(uint8_t unread, bool peerSeen, uint32_t ms);
void  drawPageReceiving(const char* fname, uint8_t pct, uint32_t elapsed,
                        uint32_t bytesRx, uint32_t bytesTotal);
void  drawPageInbox(uint8_t count, uint8_t sel, float scrollY,
                    bool playing, uint32_t playPosMs, uint32_t playTotalMs);
void  drawPageRecording(uint32_t elapsed, uint32_t recBytes, float rms);
void  drawPageSending(const char* fname, uint8_t pct, uint8_t enwState);
void  drawPageNotify(const char* fname, uint32_t age, uint8_t animFrame);
void  drawPageLiveRadio(uint8_t state, uint8_t channel, uint32_t elapsedMs,
                        uint32_t pktCount, int16_t lastSample, uint32_t ms);

#endif // DISPLAY_H