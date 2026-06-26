#ifndef AUDIO_H
#define AUDIO_H

// ════════════════════════════════════════════════════════════════
//  Audio.h — INMP441 mic recording + MAX98357A playback
//  MIC (I2S_NUM_0) and AMP (I2S_NUM_1) are mutually exclusive.
//  Raw PCM: 8kHz, 16-bit signed, mono, little-endian.
// ════════════════════════════════════════════════════════════════

#include "Config.h"
#include <driver/i2s.h>
#include <SD.h>
#include <cstdint>

// ── Audio state ───────────────────────────────────────────────
#define AUDIO_IDLE      0
#define AUDIO_RECORDING 1
#define AUDIO_PLAYING   2
#define AUDIO_ERROR     3

extern uint8_t  audioState;
extern uint32_t audioElapsedMs;
extern uint32_t audioBytes;
extern float    audioRms;          // 0..200 approx, for level meter
extern uint32_t audioPlayPosMs;
extern uint32_t audioPlayTotalMs;

// ── Lifecycle ─────────────────────────────────────────────────
void audioKillAll();

// ── Recording ─────────────────────────────────────────────────
bool audioStartRecord(const char* outPath);
void audioStopRecord();
void audioRecordTick();   // call from loop() while AUDIO_RECORDING

// ── Playback ──────────────────────────────────────────────────
bool audioStartPlay(const char* srcPath);
void audioStopPlay();
void audioPlayTick();     // call from loop() while AUDIO_PLAYING

#endif // AUDIO_H
