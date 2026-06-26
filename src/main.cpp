#include <Arduino.h>
// ════════════════════════════════════════════════════════════════
//  WalkieTalkieReceiver.ino — Main entry point
//  ESP32 Walkie-Talkie Receiver
//  Compatible with tracker_v6.4+ sender firmware.
//  Hardware:
//    SSD1306  128×64 OLED   I2C  (SDA=21, SCL=22)
//    INMP441  Microphone    I2S0 (SCK=26, WS=25, SD=34)
//    MAX98357A Amplifier    I2S1 (BCLK=27, LRCLK=25, DIN=33)
//    SD Module              SPI  (SCK=18, MISO=19, MOSI=23, CS=5)
//    BTN_PREV  GPIO13  (PREV / PTT long-hold)
//    BTN_SEL   GPIO12  (Select / play / stop)
//    BTN_NEXT  GPIO14  (Next / forward)
//
//  All pins configurable in Config.h.
//
//  Required libraries (install via Arduino Library Manager):
//    Adafruit SSD1306
//    Adafruit GFX Library
//    ESP32 Arduino core (includes esp_now, driver/i2s, WiFi)
//    SD (built-in)
// ════════════════════════════════════════════════════════════════

#include "Config.h"
#include "Display.h"
#include "Buttons.h"
#include "SDCard.h"
#include "Audio.h"
#include "ESPNow.h"
#include "Notify.h"
#include "UI.h"
#include "LiveRadio.h"   // Real-time ESP-NOW voice (Live Radio)
#include "Settings.h"

// ── Timing ────────────────────────────────────────────────────
static uint32_t _lastDisplayMs  = 0;
static uint32_t _lastNotifyMs   = 0;

// ════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(200);

    // ── Boot banner ───────────────────────────────────────────
    Serial.println("\n");
    Serial.println("╔══════════════════════════════════════╗");
    Serial.println("║  Walkie-Talkie Receiver  v1.0        ║");
    Serial.println("║  ESP32  —  Debug build               ║");
    Serial.println("╚══════════════════════════════════════╝");
    Serial.printf( "[BOOT] Chip cores : %d\n",      ESP.getChipCores());
    Serial.printf( "[BOOT] CPU freq   : %d MHz\n",  getCpuFrequencyMhz());
    Serial.printf( "[BOOT] Free heap  : %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    Serial.printf( "[BOOT] Flash size : %lu bytes\n", (unsigned long)ESP.getFlashChipSize());
    Serial.println("[BOOT] ─────────────────────────────────");
    Serial.println("[BOOT] Starting subsystem init...");

    // ── OLED ──────────────────────────────────────────────────
    Serial.print("[BOOT] OLED SSD1306 (I2C)... ");
    if (!dispInit()) {
        Serial.println("FAILED ✗");
        Serial.println("       → Check SDA=21 SCL=22 wiring");
        Serial.println("       → Check 3.3V on OLED VCC");
        Serial.println("       → Verify I2C address 0x3C in Config.h");
        // Can't show error — just continue
    } else {
        Serial.println("OK ✓");
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        // Boot splash
        dispStrC(2,  "WALKIE-TALKIE", 1);
        dispStrC(14, "RECEIVER", 2);
        dispStrC(36, "v1.0", 1);
        oled.drawRect(10, 44, 108, 6, SSD1306_WHITE);
        oled.display();
    }

    // ── Buttons ───────────────────────────────────────────────
    Serial.print("[BOOT] Buttons GPIO (PREV=13 SEL=15 NEXT=4)... ");
    btnsInit();
    Serial.println("OK ✓");

    // ── SD card ───────────────────────────────────────────────
    Serial.print("[BOOT] SD card (SPI CS=5)... ");
    bool sdOk = sdInit();
    if (sdOk) {
        Serial.printf("OK ✓  (%d files in inbox)\n", sdFileCount);
    } else {
        Serial.println("FAILED ✗");
        Serial.println("       → Check CS=5 SCK=17 MOSI=23 MISO=19 wiring");
        Serial.println("       → SD must be FAT32 formatted");
        Serial.println("       → Check 3.3V on SD VCC");
        Serial.println("       → App will run but cannot save/play audio");
    }

    // Boot bar step 1
    oled.fillRect(11, 45, 34, 4, SSD1306_WHITE);
    oled.display();
    delay(150);

    // ── ESP-NOW ───────────────────────────────────────────────
    Serial.print("[BOOT] ESP-NOW (WiFi ch=1 AP=TRACKER_ENW)... ");
    enwReceiverInit();
    Serial.println("OK ✓");
    {
        uint8_t mac[6]; WiFi.macAddress(mac);
        Serial.printf("[BOOT] Our MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        Serial.println("       → Sender must target this MAC or use broadcast");
    }

    // Boot bar step 2
    oled.fillRect(11, 45, 68, 4, SSD1306_WHITE);
    oled.display();
    delay(150);

    // ── Notify ─────────────────────────────────────────────────
    Serial.print("[BOOT] Notify (buzzer=");
    Serial.print(BUZZER_PIN);
    Serial.print(")... ");
    notifyInit();
    Serial.println("OK ✓");

    // ── Live Radio ─────────────────────────────────────────────
    Serial.printf("[BOOT] Live Radio (ch=%d)... ", LR_DEFAULT_CHANNEL);
    lrInit(LR_DEFAULT_CHANNEL);    // must be called AFTER enwReceiverInit()
    Serial.println("OK ✓");

    // ── Settings ──────────────────────────────────────────────────────
    Serial.print("[BOOT] Settings NVS... ");
    settingsInit();
    Serial.println("OK ✓");

    // ── UI ───────────────────────────────────────────────────────────
    Serial.print("[BOOT] UI state machine... ");
    uiInit();
    Serial.println("OK ✓");

    // ── Final summary ─────────────────────────────────────────
    Serial.println("[BOOT] ─────────────────────────────────");
    Serial.printf( "[BOOT] Free heap after init : %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    Serial.printf( "[BOOT] SD ready  : %s\n", sdReady  ? "YES" : "NO  ← WARNING");
    Serial.printf( "[BOOT] Inbox     : %d file(s)\n", sdFileCount);
    Serial.println("[BOOT] ─────────────────────────────────");
    Serial.println("[BOOT] Entering main loop.");
    Serial.println("[BOOT] Button events will print below.");
    Serial.println("[BOOT] ═════════════════════════════════");

    // Boot complete splash
    oled.clearDisplay();
    dispStrC(8,  "READY", 2);
    dispStrC(30, "Waiting for", 1);
    dispStrC(40, "messages...", 1);
    if (!sdOk) {
        dispStrC(52, "SD: NOT FOUND", 1);
    }
    oled.display();
    delay(1200);

    Serial.println("[BOOT] Setup complete");
}

// ════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════
void loop() {
    uint32_t now = millis();

    // ── Button events ─────────────────────────────────────────
    uint8_t btnEv = btnsTick();
    if (btnEv != BTN_NONE) {
        static const char* _evNames[] = {
            "NONE","PREV_SHORT","SEL_SHORT","NEXT_SHORT",
            "PREV_LONG","SEL_LONG","NEXT_LONG","PTT_DOWN","PTT_UP"
        };
        const char* evName = (btnEv < 9) ? _evNames[btnEv] : "UNKNOWN";
        Serial.printf("[BTN] %-12s  page=%d\n", evName, uiPage);
        uiHandleBtn(btnEv);
        Serial.printf("[BTN] → page now=%d\n", uiPage);
    }

    // ── Periodic heap + state watchdog (every 10 s) ─────────────
    static uint32_t _debugHeapMs = 0;
    if (now - _debugHeapMs >= 10000) {
        _debugHeapMs = now;
        Serial.printf("[DBG] heap=%lu  page=%d  audioState=%d  rxState=%d  lrState=%d\n",
                      (unsigned long)ESP.getFreeHeap(),
                      uiPage, audioState, rxState, lrState);
    }

    // ── Audio ticks ────────────────────────────────────────
    if (audioState == AUDIO_RECORDING) audioRecordTick();
    if (audioState == AUDIO_PLAYING)   audioPlayTick();

    // ── Live Radio tick (TX capture + RX playback) ────────────
    lrTick();

    // ── ESP-NOW ticks ─────────────────────────────────────────
    enwReceiverTick();
    enwSenderTick();

    // ── Notification tick (buzzer sequencer) ──────────────────
    if (now - _lastNotifyMs >= 20) {
        _lastNotifyMs = now;
        notifyTick();
    }

    // ── Display tick ──────────────────────────────────────────
    if (audioState != AUDIO_PLAYING) {
        if (now - _lastDisplayMs >= DISPLAY_TICK_MS) {
            _lastDisplayMs = now;
            uiTick();
        }
    } else {
        // During playback: refresh at reduced rate (max 4fps) to avoid stall
        if (now - _lastDisplayMs >= 250) {
            _lastDisplayMs = now;
            uiTick();
        }
    }
}
