// ════════════════════════════════════════════════════════════════
//  Audio.cpp — INMP441 mic + MAX98357A amp  (ESP32 Receiver)
//  8 kHz / 16-bit mono PCM — store-and-forward WT messages
//  Compatible with ESP32 Board Manager v3.0 + Arduino IDE 2.3.6
// ════════════════════════════════════════════════════════════════

#include "Audio.h"
#include <Arduino.h>
#include <cmath>
#include "Settings.h"

// ── State ─────────────────────────────────────────────────────
uint8_t  audioState       = AUDIO_IDLE;
uint32_t audioElapsedMs   = 0;
uint32_t audioBytes       = 0;
float    audioRms         = 0.0f;
uint32_t audioPlayPosMs   = 0;
uint32_t audioPlayTotalMs = 0;

// ── Internal ──────────────────────────────────────────────────
static File     _recFile;
static File     _playFile;
static uint32_t _startMs   = 0;
static bool     _micInst   = false;
static bool     _ampInst   = false;

// ── Buffers ───────────────────────────────────────────────────
#define RAW_BUF_SAMPLES  1024   // larger read block to reduce buffer underrun
#define REC_WRITE_BUF   1024   // larger write buffer for smoother SD writes
static int32_t  _rawBuf[RAW_BUF_SAMPLES];
static int16_t  _pcmBuf[RAW_BUF_SAMPLES];
static uint8_t  _wrBuf[REC_WRITE_BUF];
static uint16_t _wrFill = 0;
static uint8_t  _playBuf[1024];  // was 512 — must be >= dma_buf_len*2 bytes to prevent underrun
static float _audLpfState = 0.0f; // Low-pass filter state
static float _dcBlockX = 0.0f; // DC blocker previous input
static float _dcBlockY = 0.0f; // DC blocker previous output

static inline float applyDCBlock(float x) {
    float y = x - _dcBlockX + DC_BLOCK_ALPHA * _dcBlockY;
    _dcBlockX = x;
    _dcBlockY = y;
    return y;
}
static inline float applyLPF(float x) {
    _audLpfState = _audLpfState * (1.0f - LPF_ALPHA) + x * LPF_ALPHA;
    return _audLpfState;
}
static inline void updateRMS(float x) {
    audioRms = audioRms * RMS_SMOOTHING + fabsf(x) * (1.0f - RMS_SMOOTHING);
}
// ════════════════════════════════════════════════════════════════
//  INTERNAL HELPERS
// ════════════════════════════════════════════════════════════════
static bool installMic() {
    Serial.printf("[AUD] installMic: I2S_NUM_0  SCK=%d WS=%d SD=%d rate=%d\n",
                  MIC_SCK_PIN, MIC_WS_PIN, MIC_SD_PIN, MIC_SAMPLE_RATE);
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = MIC_SAMPLE_RATE, // Running directly at 16kHz with APLL enabled for crystal-clear stability
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 64,
        .use_apll             = true,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };
    i2s_pin_config_t pins = {
        .bck_io_num   = MIC_SCK_PIN,
        .ws_io_num    = MIC_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = MIC_SD_PIN
    };
    if (i2s_driver_install(MIC_PORT, &cfg, 0, NULL) != ESP_OK) {
        Serial.println("[AUD] ✗ i2s_driver_install(MIC) failed");
        Serial.println("      → Check if I2S_NUM_0 already in use");
        Serial.println("      → Call audioKillAll() before re-init");
        return false;
    }
    if (i2s_set_pin(MIC_PORT, &pins) != ESP_OK) {
        Serial.println("[AUD] ✗ i2s_set_pin(MIC) failed");
        Serial.printf( "      → SCK=%d WS=%d SD=%d — check wiring\n",
                       MIC_SCK_PIN, MIC_WS_PIN, MIC_SD_PIN);
        i2s_driver_uninstall(MIC_PORT); return false;
    }
    Serial.println("[AUD] ✓ Mic I2S installed");
    i2s_zero_dma_buffer(MIC_PORT);
    vTaskDelay(pdMS_TO_TICKS(40)); // non‑blocking delay
    _micInst = true;
    return true;
}

static bool installAmp() {
    Serial.printf("[AUD] installAmp: I2S_NUM_1  BCLK=%d LRCLK=%d DIN=%d rate=%d\n",
                  AMP_BCLK_PIN, AMP_LRCLK_PIN, AMP_DIN_PIN, AMP_SAMPLE_RATE);
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = AMP_SAMPLE_RATE, // Upgrade to 16kHz
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 128,
        .use_apll             = true,

        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0
    };
    i2s_pin_config_t pins = {
        .bck_io_num   = AMP_BCLK_PIN,
        .ws_io_num    = AMP_LRCLK_PIN,
        .data_out_num = AMP_DIN_PIN,
        .data_in_num  = I2S_PIN_NO_CHANGE
    };
    if (i2s_driver_install(AMP_PORT, &cfg, 0, NULL) != ESP_OK) {
        Serial.println("[AUD] ✗ i2s_driver_install(AMP) failed");
        Serial.println("      → Check if I2S_NUM_1 already in use");
        return false;
    }
    if (i2s_set_pin(AMP_PORT, &pins) != ESP_OK) {
        Serial.println("[AUD] ✗ i2s_set_pin(AMP) failed");
        Serial.printf( "      → BCLK=%d LRCLK=%d DIN=%d — check wiring\n",
                       AMP_BCLK_PIN, AMP_LRCLK_PIN, AMP_DIN_PIN);
        i2s_driver_uninstall(AMP_PORT); return false;
    }
    Serial.println("[AUD] ✓ Amp I2S installed");
    i2s_zero_dma_buffer(AMP_PORT);
    vTaskDelay(pdMS_TO_TICKS(20)); // non‑blocking delay
    _ampInst = true;
    return true;
}

static void flushWriteBuf() {
    if (!_recFile || _wrFill == 0) return;
    _recFile.write(_wrBuf, _wrFill);
    audioBytes += _wrFill;
    _wrFill = 0;
}

// ════════════════════════════════════════════════════════════════
//  KILL ALL I2S
// ════════════════════════════════════════════════════════════════
void audioKillAll() {
    if (_ampInst) {
        i2s_zero_dma_buffer(AMP_PORT);
        vTaskDelay(pdMS_TO_TICKS(5)); // non‑blocking delay
        i2s_driver_uninstall(AMP_PORT);
        _ampInst = false;
    }
    if (_micInst) {
        i2s_zero_dma_buffer(MIC_PORT);
        vTaskDelay(pdMS_TO_TICKS(5)); // non‑blocking delay
        i2s_driver_uninstall(MIC_PORT);
        _micInst = false;
    }
    if (_recFile)  { _recFile.flush(); _recFile.close(); }
    if (_playFile) _playFile.close();
    audioState = AUDIO_IDLE;
    audioRms   = 0.0f;
    vTaskDelay(pdMS_TO_TICKS(20)); // non‑blocking delay
}

// ════════════════════════════════════════════════════════════════
//  RECORDING
// ════════════════════════════════════════════════════════════════
bool audioStartRecord(const char* outPath) {
    audioKillAll();
    if (!installMic()) {
        audioState = AUDIO_ERROR;
        return false;
    }
    _recFile = SD.open(outPath, FILE_WRITE);
    if (!_recFile) {
        Serial.printf("[AUD] ✗ SD open failed: %s\n", outPath);
        Serial.println("      → Is SD card mounted? (sdReady flag)");
        Serial.println("      → Does /WALKIE/SENT/ directory exist?");
        audioKillAll();
        audioState = AUDIO_ERROR;
        return false;
    }
    audioBytes     = 0;
    audioElapsedMs = 0;
    audioRms       = 0.0f;
    _wrFill        = 0;
    _startMs       = millis();
    audioState     = AUDIO_RECORDING;
    Serial.printf("[AUD] Record start: %s\n", outPath);
    return true;
}

void audioStopRecord() {
    if (audioState != AUDIO_RECORDING) return;
    audioElapsedMs = millis() - _startMs;
    // Flush write buffer BEFORE touching I2S or closing file
    flushWriteBuf();
    if (_recFile) { _recFile.flush(); _recFile.close(); }
    // Uninstall mic only (amp not used during recording)
    if (_micInst) {
        i2s_zero_dma_buffer(MIC_PORT);
        i2s_driver_uninstall(MIC_PORT);
        _micInst = false;
    }
    audioState = AUDIO_IDLE;
    Serial.printf("[AUD] Record stop: %lu ms, %lu bytes\n",
                  audioElapsedMs, audioBytes);
}

void audioRecordTick() {
    if (audioState != AUDIO_RECORDING) return;
    audioElapsedMs = millis() - _startMs;

    if (audioElapsedMs >= MAX_RECORD_MS) {
        audioStopRecord();
        return;
    }

    size_t bytesRead = 0;
    // Small timeout (10ms) prevents empty reads without long blocking
    // Non‑blocking read: timeout 0 returns immediately if no data
    i2s_read(MIC_PORT, _rawBuf,
             RAW_BUF_SAMPLES * sizeof(int32_t),
             &bytesRead, 0);
    if (bytesRead == 0) return;

    int samples = bytesRead / sizeof(int32_t);
    int32_t sumSq = 0;
    int outSamples = 0;

    for (int i = 0; i < samples; i++) {
        float x = (float)(_rawBuf[i] >> 13);
        x = applyDCBlock(x);
        x = applyLPF(x);
        updateRMS(x);

        float gateGain;
        if (audioRms >= NOISE_GATE_THRESHOLD) {
            gateGain = GAIN_BOOST;
        } else if (audioRms <= NOISE_GATE_THRESHOLD * 0.25f) {
            gateGain = 0.0f;
        } else {
            float ratio = (audioRms - NOISE_GATE_THRESHOLD * 0.25f) / (NOISE_GATE_THRESHOLD * 0.75f);
            gateGain = GAIN_BOOST * ratio;
        }

        float b = x * gateGain;
        const float KNEE  = 26000.0f;
        const float RANGE = 32767.0f - KNEE;
        if      (b >  KNEE) b =  KNEE + RANGE * tanhf((b  - KNEE) / RANGE);
        else if (b < -KNEE) b = -KNEE - RANGE * tanhf((-b - KNEE) / RANGE);

        int16_t out = (int16_t)b;
        _pcmBuf[i] = out;
        sumSq += (int32_t)out * out;
    }

    // Write to SD
    for (int i = 0; i < samples; i++) {
        _wrBuf[_wrFill++] = (uint8_t)(_pcmBuf[i] & 0xFF);
        _wrBuf[_wrFill++] = (uint8_t)((_pcmBuf[i] >> 8) & 0xFF);
        if (_wrFill >= REC_WRITE_BUF) flushWriteBuf();
    }
    audioBytes += samples * 2;

    float rmsNow = (samples > 0) ? sqrtf((float)sumSq / samples) : 0.0f;
    audioRms = audioRms * 0.8f + rmsNow * 0.2f;

    // Warn if mic appears silent (only print once per second)
    static uint32_t _silenceWarnMs = 0;
    if (audioRms < 2.0f && millis() - _silenceWarnMs > 1000) {
        _silenceWarnMs = millis();
        Serial.printf("[AUD] WARNING: very low RMS=%.1f — mic silent?\n", audioRms);
        Serial.println("      → Check INMP441 L/R pin tied to GND (left channel)");
        Serial.printf( "      → SD pin = GPIO%d, confirm wiring\n", MIC_SD_PIN);
    }
}

// ════════════════════════════════════════════════════════════════
//  PLAYBACK
// ════════════════════════════════════════════════════════════════
bool audioStartPlay(const char* srcPath) {
    audioKillAll();
    if (!installAmp()) {
        audioState = AUDIO_ERROR;
        return false;
    }
    _playFile = SD.open(srcPath, FILE_READ);
    if (!_playFile) {
        Serial.printf("[AUD] ✗ SD open failed for playback: %s\n", srcPath);
        Serial.println("      → File may have been deleted or path is wrong");
        Serial.println("      → Call sdScanInbox() and check sdFiles[]");
        audioKillAll();
        audioState = AUDIO_ERROR;
        return false;
    }
    audioPlayTotalMs = (uint32_t)((uint64_t)_playFile.size() * 1000
                                  / (AMP_SAMPLE_RATE * 2));  // 16-bit = 2 bytes/sample
    audioPlayPosMs   = 0;
    _startMs         = millis();
    audioState       = AUDIO_PLAYING;
    Serial.printf("[AUD] Play start: %s  (%lu ms)\n", srcPath, audioPlayTotalMs);
    return true;
}

void audioStopPlay() {
    if (_playFile) _playFile.close();
    audioKillAll();
    audioState = AUDIO_IDLE;
}

void audioPlayTick() {
    if (audioState != AUDIO_PLAYING) return;

    audioPlayPosMs = millis() - _startMs;

    if (!_playFile) {
        audioStopPlay();
        return;
    }

    // Use position >= size instead of available() — SD lib can return 0 early
    if (_playFile.size() > 0 && _playFile.position() >= _playFile.size()) {
        // Drain I2S DMA before stopping so last bytes are audible
        uint8_t silence[128] = {0};
        size_t w = 0;
        i2s_write(AMP_PORT, silence, sizeof(silence), &w, 10);
        audioStopPlay();
        return;
    }

    int rd = _playFile.read(_playBuf, sizeof(_playBuf));
    if (rd <= 0) {
        audioStopPlay();
        return;
    }

    // Apply volume scaling
    float volScale = (float)setVolume / 100.0f;
    int16_t* pcm16 = (int16_t*)_playBuf;
    int numSamples = rd / 2;
    for (int i = 0; i < numSamples; i++) {
        pcm16[i] = (int16_t)(pcm16[i] * volScale);
    }

    size_t written = 0;
    // 20ms: i2s_write returns when data fits in DMA ring, not when played out
    // Non‑blocking write: timeout 0, may return ESP_ERR_TIMEOUT if DMA full
    esp_err_t err = i2s_write(AMP_PORT, _playBuf, rd, &written, 0);
    if (err != ESP_OK) {
        Serial.printf("[AUD] i2s_write error: %d\n", err);
        audioStopPlay();
    }
}