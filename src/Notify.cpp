// ════════════════════════════════════════════════════════════════
//  Notify.cpp — New-message notification  (Walkie-Talkie RX)
// ════════════════════════════════════════════════════════════════

#include "Notify.h"
#include <Arduino.h>

bool     notifyActive     = false;
char     notifyFile[48]   = "";
uint32_t notifyReceivedMs = 0;
uint8_t  notifyAnimFrame  = 0;

// ── Buzzer sequencer ──────────────────────────────────────────
static uint8_t  _buzBeep  = 0;
static uint32_t _buzNextMs = 0;
static bool     _buzOn    = false;

static void buzzerOn() {
#if BUZZER_PIN >= 0
    ledcSetup(0, NOTIFY_FREQ_HZ, 8);
    ledcAttachPin(BUZZER_PIN, 0);
    ledcWrite(0, 128);
#endif
}

static void buzzerOff() {
#if BUZZER_PIN >= 0
    ledcWrite(0, 0);
    ledcDetachPin(BUZZER_PIN);
#endif
}

void notifyInit() {
#if BUZZER_PIN >= 0
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
#endif
}

void notifyNewMessage(const char* path) {
    strncpy(notifyFile, path, 47);
    notifyReceivedMs = millis();
    notifyActive     = true;
    notifyAnimFrame  = 0;
    _buzBeep         = 0;
    _buzNextMs       = millis();
    _buzOn           = false;
    Serial.printf("[NOTIFY] New message: %s\n", path);
}

void notifyDismiss() {
    notifyActive = false;
    buzzerOff();
    _buzBeep = NOTIFY_BEEPS + 1; // stop buzzer sequence
}

void notifyTick() {
    notifyAnimFrame++;

    if (!notifyActive) return;

    uint32_t now = millis();

    // Buzzer: N beeps of 150ms on / 150ms off
    if (_buzBeep < NOTIFY_BEEPS) {
        if (!_buzOn && now >= _buzNextMs) {
            buzzerOn();
            _buzOn     = true;
            _buzNextMs = now + 150;
        } else if (_buzOn && now >= _buzNextMs) {
            buzzerOff();
            _buzOn     = false;
            _buzNextMs = now + 150;
            _buzBeep++;
        }
    }
}
