
#ifndef LIVE_RADIO_H
#define LIVE_RADIO_H

// ════════════════════════════════════════════════════════════════
//  LiveRadio.h — Real-time ESP-NOW Walkie-Talkie (Live Radio)
//  Protocol:  Uncompressed 16-bit 16kHz Mono PCM over ESP-NOW
//  Mode:      Half-duplex PTT (Push-To-Talk)
//  Channels:  4 logical channels, filtered by LR_Packet.channel
//
//  State machine:
//    LR_IDLE  →  LR_TX : PTT pressed (lrPttDown)
//    LR_TX    →  LR_IDLE : PTT released (lrPttUp) or 8s timeout
//    LR_IDLE  →  LR_RX : ESP-NOW LR_Packet arrives (lrHandlePacket)
//    LR_RX    →  LR_IDLE : no packet for >100ms
//
//  I2S constraint (shared BCLK/LRCLK bus):
//    Mic (I2S_NUM_0) and Amp (I2S_NUM_1) CANNOT run simultaneously.
//    lrMicStart() always calls lrAmpStop() first.
//    lrAmpStart() always calls lrMicStop() first.
// ════════════════════════════════════════════════════════════════

#include "Config.h"
#include <driver/i2s.h>
#include <cstdint>

// ── Live Radio State IDs ──────────────────────────────────────
#define LR_IDLE  0
#define LR_TX    1
#define LR_RX    2

// ── Channel Names ─────────────────────────────────────────────
// CH-0 ALPHA   target MAC: FF:FF:FF:FF:FF:01
// CH-1 BRAVO   target MAC: FF:FF:FF:FF:FF:02
// CH-2 CHARLIE target MAC: FF:FF:FF:FF:FF:03
// CH-3 DELTA   target MAC: FF:FF:FF:FF:FF:04
#define LR_CHANNEL_COUNT  4
static const char* const LR_CHANNEL_NAMES[LR_CHANNEL_COUNT] = {
    "ALPHA", "BRAVO", "CHARLIE", "DELTA"
};

// ── Wire Packet Struct (must match sender exactly) ────────────
// Total: 1+1+1+1+2+2+240 = 249 bytes  (fits in ESP-NOW 250B max)
#pragma pack(push, 1)
typedef struct {
    uint8_t  magic;              // Fixed 0xAD — identifies Live Radio pkt
    uint8_t  channel;            // Logical channel 0-3
    uint8_t  seq;                // Sequence number 0-255 (wrapping)
    uint8_t  total;              // Total pkts in burst (0 = continuous)
    uint16_t sampleRate;         // 16000
    uint16_t numSamples;         // 120
    int16_t  samples[120];       // 240 bytes uncompressed PCM
} LR_Packet;
#pragma pack(pop)

#define LR_MAGIC  0xAD

// ── Exposed State Variables ───────────────────────────────────
extern uint8_t   lrState;           // LR_IDLE / LR_TX / LR_RX
extern uint8_t   lrChannel;         // active channel (0-3)
extern uint32_t  lrLastPacketMs;    // millis() of last received LR packet
extern uint32_t  lrTxStartMs;       // millis() when current TX started
extern uint32_t  lrRxPacketCount;   // packets received in current RX session
extern uint32_t  lrTxPacketCount;   // packets sent in current TX session
extern int16_t   lrLastSample;      // last processed sample (for VU meter)

// ── Lifecycle ────────────────────────────────────────────────
// Call lrInit() once in setup() — after enwReceiverInit() (WiFi must be up)
void lrInit(uint8_t channel);

// Call lrTick() from loop() every iteration
void lrTick();

// ── PTT Control ──────────────────────────────────────────────
void lrPttDown();    // begin transmit: stops amp, starts mic, sends packets
void lrPttUp();      // end transmit:   stops mic

// ── Channel Selection ─────────────────────────────────────────
void lrSetChannel(uint8_t ch);   // 0-3; ignored if TX/RX active

// ── Packet Ingestion (called from ESPNow.cpp recv callback) ──
// Returns true if the packet was a valid LR_Packet and was consumed
bool lrHandlePacket(const uint8_t* data, int len);

// ── I2S Helpers (also useful for Audio.cpp collision avoidance) ─
void lrMicStart();   // install I2S_NUM_0 as mic input
void lrMicStop();    // uninstall I2S_NUM_0
void lrAmpStart();   // install I2S_NUM_1 as amp output
void lrAmpStop();    // uninstall I2S_NUM_1

// ── Convenience ──────────────────────────────────────────────
// Returns true if the raw data looks like an LR_Packet (quick header check)
static inline bool lrIsLRPacket(const uint8_t* data, int len) {
    return (len >= (int)sizeof(LR_Packet) && data[0] == LR_MAGIC);
}

#endif // LIVE_RADIO_H
