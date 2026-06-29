// ════════════════════════════════════════════════════════════════
//  LiveRadio.cpp — Real-time ESP-NOW Walkie-Talkie engine
//  Walkie-Talkie Receiver  (ESP32)
//
//  State machine (half-duplex):
//    LR_IDLE → LR_TX : lrPttDown()
//    LR_TX   → LR_IDLE : lrPttUp() or 8-second safety cap
//    LR_IDLE → LR_RX : incoming LR_Packet with matching channel
//    LR_RX   → LR_IDLE : no packet for >100ms (LR_RX_TIMEOUT_MS)
//
//  I2S shared-bus constraint:
//    MIC (I2S_NUM_0) and AMP (I2S_NUM_1) share BCLK+LRCLK lines.
//    They must NEVER be installed simultaneously.
//    lrMicStart() calls lrAmpStop() and audioKillAll() first.
//    lrAmpStart() calls lrMicStop() and audioKillAll() first.
//
//  TX audio pipeline (from specs §3):
//    1. Read 32-bit I2S frames from INMP441
//    2. Shift right 14 bits → int16_t   (upper 24 bits carry audio)
//    3. Multiply × 2 (digital gain boost)
//    4. Constrain to [-32768, 32767]
//    5. Pack 120 samples into LR_Packet and broadcast via ESP-NOW
//
//  RX audio pipeline:
//    1. Receive LR_Packet in ESP-NOW callback → push to ring buffer
//    2. In lrTick(): pop packets, write int16_t samples to I2S AMP
//    3. After 100ms silence → stop AMP, return to LR_IDLE
// ════════════════════════════════════════════════════════════════

#include "LiveRadio.h"
#include "Audio.h"        // for audioKillAll()
#include "Settings.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>     // For dynamic MAC address configuration
#include <driver/i2s.h>
#include <string.h>

// ════════════════════════════════════════════════════════════════
//  COMPILE-TIME CONSTANTS
//  These are defined in Config.h:
//  LR_CHUNK_SAMPLES, LR_RX_QUEUE_SIZE, LR_RX_TIMEOUT_MS, LR_TX_MAX_MS
// ════════════════════════════════════════════════════════════════

// Channel broadcast MACs (sender unicasts to these; we broadcast from these)
static const uint8_t LR_CHANNEL_MACS[LR_CHANNEL_COUNT][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01},  // CH-0 ALPHA
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02},  // CH-1 BRAVO
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x03},  // CH-2 CHARLIE
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x04},  // CH-3 DELTA
};

// ════════════════════════════════════════════════════════════════
//  EXPOSED STATE
// ════════════════════════════════════════════════════════════════

uint8_t  lrState          = LR_IDLE;
uint8_t  lrChannel        = 0;
uint32_t lrLastPacketMs   = 0;
uint32_t lrTxStartMs      = 0;
uint32_t lrRxPacketCount  = 0;
uint32_t lrTxPacketCount  = 0;
int16_t  lrLastSample     = 0;

// ════════════════════════════════════════════════════════════════
//  PRIVATE STATE
// ════════════════════════════════════════════════════════════════

static bool     _micOpen   = false;
static bool     _ampOpen   = false;

// Circular ring buffer (ISR-safe via head/tail volatile)
static LR_Packet         _rxQueue[LR_RX_QUEUE_SIZE];
static volatile uint8_t  _queueHead = 0;  // write pointer (ISR writes)
static volatile uint8_t  _queueTail = 0;  // read  pointer (main loop reads)

static uint8_t  _txSeq     = 0;           // TX sequence counter
static int32_t  _lrDcOffset = 0;          // DC bias estimate (IIR)
static int16_t  _txAccumulator[LR_CHUNK_SAMPLES];
static uint16_t _txAccumulatorFill = 0;

// DSP state for Live Radio Mic (matching Audio.cpp)
static float _lrDcBlockX = 0.0f;
static float _lrDcBlockY = 0.0f;
static float _lrLpfState = 0.0f;
static float _lrAudioRms = 0.0f;

static inline float lrApplyDCBlock(float x) {
    float y = x - _lrDcBlockX + DC_BLOCK_ALPHA * _lrDcBlockY;
    _lrDcBlockX = x;
    _lrDcBlockY = y;
    return y;
}

static inline float lrApplyLPF(float x) {
    _lrLpfState = _lrLpfState * (1.0f - LPF_ALPHA) + x * LPF_ALPHA;
    return _lrLpfState;
}

static inline void lrUpdateRMS(float x) {
    _lrAudioRms = _lrAudioRms * RMS_SMOOTHING + fabsf(x) * (1.0f - RMS_SMOOTHING);
}

// ════════════════════════════════════════════════════════════════
//  I2S — MICROPHONE  (I2S_NUM_0 / INMP441)
// ════════════════════════════════════════════════════════════════

void lrAmpStop();   // forward declaration

void lrMicStart() {
    if (_micOpen) return;

    // Always stop AMP first — shared BCLK/LRCLK bus constraint
    lrAmpStop();
    audioKillAll();   // make sure Audio.cpp is not using I2S_NUM_0

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate          = LR_SAMPLE_RATE; // Running directly at 16kHz with APLL enabled for crystal-clear stability
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count        = 8; // increased to 8 to handle OLED redraw delays
    cfg.dma_buf_len          = 128; // increased to 128 to handle OLED redraw delays
    cfg.use_apll             = true;
    cfg.tx_desc_auto_clear   = false;
    cfg.fixed_mclk           = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num    = MIC_SCK_PIN;
    pins.ws_io_num     = MIC_WS_PIN;
    pins.data_out_num  = I2S_PIN_NO_CHANGE;
    pins.data_in_num   = MIC_SD_PIN;

    esp_err_t err = i2s_driver_install(MIC_PORT, &cfg, 0, NULL);
    if (err == ESP_OK) {
        i2s_set_pin(MIC_PORT, &pins);
        i2s_start(MIC_PORT);
        _micOpen = true;
        Serial.println("[LR] Mic started (I2S_NUM_0)");
    } else {
        Serial.printf("[LR] Mic start FAILED: %d\n", err);
    }
}

void lrMicStop() {
    if (!_micOpen) return;
    i2s_stop(MIC_PORT);
    i2s_driver_uninstall(MIC_PORT);
    _micOpen = false;
    Serial.println("[LR] Mic stopped");
}

// ════════════════════════════════════════════════════════════════
//  I2S — AMPLIFIER  (I2S_NUM_1 / MAX98357A)
// ════════════════════════════════════════════════════════════════

void lrMicStop();   // forward declaration

void lrAmpStart() {
    if (_ampOpen) return;

    // Always stop MIC first — shared BCLK/LRCLK bus constraint
    lrMicStop();
    audioKillAll();   // make sure Audio.cpp is not using I2S_NUM_1

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = AMP_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 256;
    cfg.use_apll             = true;
    cfg.tx_desc_auto_clear   = true;
    cfg.fixed_mclk           = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num    = AMP_BCLK_PIN;
    pins.ws_io_num     = AMP_LRCLK_PIN;
    pins.data_out_num  = AMP_DIN_PIN;
    pins.data_in_num   = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(AMP_PORT, &cfg, 0, NULL);
    if (err == ESP_OK) {
        i2s_set_pin(AMP_PORT, &pins);
        i2s_start(AMP_PORT);
        _ampOpen = true;
        Serial.println("[LR] Amp started (I2S_NUM_1)");
    } else {
        Serial.printf("[LR] Amp start FAILED: %d\n", err);
    }
}

void lrAmpStop() {
    if (!_ampOpen) return;
    i2s_stop(AMP_PORT);
    i2s_driver_uninstall(AMP_PORT);
    _ampOpen = false;

    // Pull AMP DIN pin LOW to prevent floating noise/feedback
    pinMode(AMP_DIN_PIN, OUTPUT);
    digitalWrite(AMP_DIN_PIN, LOW);

    Serial.println("[LR] Amp stopped");
}

// ════════════════════════════════════════════════════════════════
//  RING BUFFER HELPERS
// ════════════════════════════════════════════════════════════════

static inline bool _queueFull() {
    return (((_queueHead + 1) % LR_RX_QUEUE_SIZE) == _queueTail);
}

static inline bool _queueEmpty() {
    return (_queueHead == _queueTail);
}

// Push packet (called from ESP-NOW callback — may be ISR context)
static void _queuePush(const LR_Packet* pkt) {
    uint8_t nextHead = (_queueHead + 1) % LR_RX_QUEUE_SIZE;
    if (nextHead == _queueTail) return; // drop if full
    memcpy(&_rxQueue[_queueHead], pkt, sizeof(LR_Packet));
    _queueHead = nextHead;
}

// Pop packet (called from main loop only)
static bool _queuePop(LR_Packet* out) {
    if (_queueEmpty()) return false;
    memcpy(out, &_rxQueue[_queueTail], sizeof(LR_Packet));
    _queueTail = (_queueTail + 1) % LR_RX_QUEUE_SIZE;
    return true;
}

// ════════════════════════════════════════════════════════════════
//  ESP-NOW PACKET HANDLER
//  Called from ESPNow.cpp enwOnRecvNew() when magic == 0xAD
// ════════════════════════════════════════════════════════════════

bool lrHandlePacket(const uint8_t* data, int len) {
    if (len < (int)sizeof(LR_Packet)) return false;

    const LR_Packet* pkt = (const LR_Packet*)data;
    if (pkt->magic   != LR_MAGIC)  return false;
    if (pkt->channel != lrChannel) return false;  // wrong channel — ignore

    // Push into ring buffer regardless of current state
    _queuePush(pkt);
    lrLastPacketMs = millis();

    // Transition to LR_RX if we were idle (state change picked up in lrTick)
    if (lrState == LR_IDLE) {
        lrState = LR_RX;
        lrRxPacketCount = 0;
        Serial.printf("[LR] → LR_RX  ch=%d\n", lrChannel);
    }
    lrRxPacketCount++;
    return true;
}

// ════════════════════════════════════════════════════════════════
//  ESP-NOW SEND HELPER
// ════════════════════════════════════════════════════════════════

static void _sendPacket(const LR_Packet* pkt) {
    // Broadcast to the channel's virtual MAC so all listening devices receive
    const uint8_t* dest = LR_CHANNEL_MACS[lrChannel];

    // Add peer if not already registered
    if (!esp_now_is_peer_exist(dest)) {
        esp_now_peer_info_t pi = {};
        memcpy(pi.peer_addr, dest, 6);
        pi.channel = 0;       // 0 = use current WiFi channel
        pi.encrypt = false;
        esp_now_add_peer(&pi);
    }

    esp_now_send(dest, (const uint8_t*)pkt, sizeof(LR_Packet));
}

// ════════════════════════════════════════════════════════════════
//  INIT
// ════════════════════════════════════════════════════════════════

void lrInit(uint8_t channel) {
    // Drive AMP DIN pin LOW to prevent startup buzz
    pinMode(AMP_DIN_PIN, OUTPUT);
    digitalWrite(AMP_DIN_PIN, LOW);

    lrChannel       = channel < LR_CHANNEL_COUNT ? channel : 0;
    lrState         = LR_IDLE;
    lrLastPacketMs  = 0;
    lrTxStartMs     = 0;
    lrRxPacketCount = 0;
    lrTxPacketCount = 0;
    _micOpen        = false;
    _ampOpen        = false;
    _queueHead      = 0;
    _queueTail      = 0;
    _txSeq          = 0;

    // Register the channel's virtual MAC as a peer to receive incoming broadcasts
    if (!esp_now_is_peer_exist(LR_CHANNEL_MACS[lrChannel])) {
        esp_now_peer_info_t pi = {};
        memcpy(pi.peer_addr, LR_CHANNEL_MACS[lrChannel], 6);
        pi.channel = 0;       // 0 = use current WiFi channel
        pi.encrypt = false;
        esp_now_add_peer(&pi);
    }

    Serial.printf("[LR] Initialized  ch=%d (%s)  RX_TIMEOUT=%dms  TX_MAX=%dms\n",
                  lrChannel, LR_CHANNEL_NAMES[lrChannel],
                  LR_RX_TIMEOUT_MS, LR_TX_MAX_MS);
}

// ════════════════════════════════════════════════════════════════
//  PTT CONTROL
// ════════════════════════════════════════════════════════════════

void lrPttDown() {
    if (lrState == LR_TX) return;          // already transmitting
    if (lrState == LR_RX) {
        // Can't TX while receiving — drop request
        Serial.println("[LR] PTT ignored — currently in LR_RX");
        return;
    }

    // Stop amp first (shared bus), then start mic
    lrAmpStop();
    lrMicStart();

    if (!_micOpen) {
        Serial.println("[LR] PTT FAILED — mic did not start");
        return;
    }

    lrState         = LR_TX;
    lrTxStartMs     = millis();
    lrTxPacketCount = 0;
    _txSeq          = 0;
    _txAccumulatorFill = 0;


    Serial.printf("[LR] → LR_TX  ch=%d (%s)\n",
                  lrChannel, LR_CHANNEL_NAMES[lrChannel]);
}

void lrPttUp() {
    if (lrState != LR_TX) return;

    lrMicStop();
    lrState = LR_IDLE;

    Serial.printf("[LR] → LR_IDLE  (TX done  pkts=%lu  dur=%lums)\n",
                  lrTxPacketCount, millis() - lrTxStartMs);
}

// ════════════════════════════════════════════════════════════════
//  CHANNEL SELECTION
// ════════════════════════════════════════════════════════════════

void lrSetChannel(uint8_t ch) {
    if (lrState != LR_IDLE) return;   // don't change mid-session
    if (ch >= LR_CHANNEL_COUNT) return;
    // Remove old peer if exists
    if (esp_now_is_peer_exist(LR_CHANNEL_MACS[lrChannel])) {
        esp_now_del_peer(LR_CHANNEL_MACS[lrChannel]);
    }

    lrChannel = ch;

    // Register new peer to listen on the new channel's virtual MAC
    if (!esp_now_is_peer_exist(LR_CHANNEL_MACS[lrChannel])) {
        esp_now_peer_info_t pi = {};
        memcpy(pi.peer_addr, LR_CHANNEL_MACS[lrChannel], 6);
        pi.channel = 0;
        pi.encrypt = false;
        esp_now_add_peer(&pi);
    }

    Serial.printf("[LR] Channel → %d (%s)\n", ch, LR_CHANNEL_NAMES[ch]);
}

// ════════════════════════════════════════════════════════════════
//  MAIN TICK — call from loop() every iteration
// ════════════════════════════════════════════════════════════════

// TX scratch buffers (static to avoid stack pressure)
static int32_t  _txRaw[LR_CHUNK_SAMPLES];    // 32-bit raw DMA samples (at 16kHz)
static int16_t  _txPcm[LR_CHUNK_SAMPLES];        // processed 16-bit PCM

void lrTick() {
    uint32_t now = millis();

    // ── LR_TX: capture mic and transmit ───────────────────────
    if (lrState == LR_TX) {

        // Safety cap: disabled for unlimited PTT
        /*
        if (now - lrTxStartMs >= LR_TX_MAX_MS) {
            Serial.println("[LR] TX safety cap reached (8s) — auto stop");
            lrPttUp();
            return;
        }
        */

        if (!_micOpen) return;  // shouldn't happen, but guard

        // Drain the entire DMA buffer so we don't drop audio during OLED updates!
        while (true) {
            size_t bytesRead = 0;
            esp_err_t err = i2s_read(MIC_PORT,
                                     _txRaw,
                                     LR_CHUNK_SAMPLES * sizeof(int32_t),
                                     &bytesRead,
                                     0 /* NO TIMEOUT - pull what is ready */);
            if (err != ESP_OK || bytesRead == 0) break;

            int samplesRead = bytesRead / sizeof(int32_t);
            if (samplesRead == 0) continue;

            for (int i = 0; i < samplesRead; i++) {
                float x = (float)(_txRaw[i] >> 13);
                x = lrApplyDCBlock(x);
                x = lrApplyLPF(x);
                lrUpdateRMS(x);

                float gateGain;
                if (_lrAudioRms >= MIC_NOISE_GATE) {
                    gateGain = MIC_GAIN;
                } else if (_lrAudioRms <= MIC_NOISE_GATE * 0.25f) {
                    gateGain = 0.0f;
                } else {
                    float ratio = (_lrAudioRms - MIC_NOISE_GATE * 0.25f) / (MIC_NOISE_GATE * 0.75f);
                    gateGain = MIC_GAIN * ratio;
                }

                float b = x * gateGain;
                const float KNEE  = 26000.0f;
                const float RANGE = 32767.0f - KNEE;
                if      (b >  KNEE) b =  KNEE + RANGE * tanhf((b  - KNEE) / RANGE);
                else if (b < -KNEE) b = -KNEE - RANGE * tanhf((-b - KNEE) / RANGE);

                _txPcm[i] = (int16_t)b;
            }
            lrLastSample = _txPcm[samplesRead - 1];

            // Build LR_Packet and send
            LR_Packet pkt;
            pkt.magic      = LR_MAGIC;
            pkt.channel    = lrChannel;
            pkt.seq        = _txSeq++;
            pkt.total      = 0;           // 0 = continuous streaming
            pkt.sampleRate = (uint16_t)MIC_SAMPLE_RATE;
            pkt.numSamples = samplesRead;
            memcpy(pkt.samples, _txPcm, samplesRead * sizeof(int16_t));

            _sendPacket(&pkt);
            lrTxPacketCount++;
        }
        return;
    }

    // ── LR_RX: play incoming packets through amp ───────────────
    if (lrState == LR_RX) {

        // Start amp on first packet pop
        if (!_queueEmpty() && !_ampOpen) {
            lrAmpStart();
        }

        // Pop and play all queued packets in this tick
        LR_Packet pkt;
        while (_queuePop(&pkt)) {
            if (!_ampOpen) break;
            
            // Apply volume scaling
            float volScale = (float)setVolume / 100.0f;
            for (int i = 0; i < pkt.numSamples; i++) {
                pkt.samples[i] = (int16_t)(pkt.samples[i] * volScale);
            }
            
            lrLastSample = pkt.samples[pkt.numSamples - 1];
            size_t written = 0;
            // Write audio samples to AMP with a short timeout (non‑blocking)
            i2s_write(AMP_PORT,
                      pkt.samples,
                      pkt.numSamples * sizeof(int16_t),
                      &written,
                      5); // 5 ms timeout to keep ISR responsive
            // If the write timed‑out, the remaining samples stay in the queue and will be sent next tick

        }

        // Timeout: no packet for LR_RX_TIMEOUT_MS → go idle
        // We fetch a fresh millis() here because i2s_write can block, and
        // the ESP-NOW callback might update lrLastPacketMs in the background.
        // If we used the old 'now', we could get an unsigned underflow!
        uint32_t currentMs = millis();
        if (currentMs >= lrLastPacketMs && (currentMs - lrLastPacketMs) > LR_RX_TIMEOUT_MS) {
            lrAmpStop();
            lrState = LR_IDLE;
            Serial.printf("[LR] → LR_IDLE  (RX timeout  pkts=%lu)\n",
                          lrRxPacketCount);
        }
        return;
    }

    // ── LR_IDLE: nothing to do ────────────────────────────────
    // (Both I2S drivers are stopped; ESP-NOW recv callback handles wakeup)
}
