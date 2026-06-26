#ifndef CONFIG_H
#define CONFIG_H

// ════════════════════════════════════════════════════════════════
//  Config.h — Walkie-Talkie Receiver  (ESP32 + SSD1306 0.96")
//  Hardware: ESP32 devkit, SSD1306 OLED I2C, INMP441, MAX98357A,
//            SD card module (SPI), 3 push buttons
// ════════════════════════════════════════════════════════════════

// ── OLED (SSD1306 128×64, I2C) ────────────────────────────────
#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_I2C_ADDR   0x3C
#define OLED_W          128
#define OLED_H          64

// ── SD card (SPI) ─────────────────────────────────────────────
#define SD_CS           5
#define SD_MOSI         23
#define SD_MISO         19
#define SD_SCK          17
#define SD_PATH         "/WALKIE"
#define SD_PATH_INBOX   "/WALKIE/INBOX"
#define SD_PATH_SENT    "/WALKIE/SENT"
#define SD_PATH_FILE_RX "/WALKIE/FILE_RX"
#define SD_PATH_LIVE    "/WALKIE/LIVE"
#define SD_MAX_FILES    32

// ── Buttons (active LOW, internal pull-up) ────────────────────
#define BTN_PREV_PIN    13    // Previous / navigate left  / PTT
#define BTN_SEL_PIN     15    // Select / confirm / play
#define BTN_NEXT_PIN    4     // Next / navigate right
#define BTN_DEBOUNCE_MS 40
#define BTN_LONG_MS     700   // long press threshold

// ── INMP441 Microphone (I2S_NUM_0) ────────────────────────────
#define MIC_SCK_PIN     33
#define MIC_WS_PIN      32
#define MIC_SD_PIN      26   // data in
#define MIC_PORT        I2S_NUM_0
// FIX: was 8000 — must match sender LR_SAMPLE_RATE=16000
#define MIC_SAMPLE_RATE 16000
#define MIC_GAIN        0.8f   // reduced gain to lower mic noise
#define MIC_NOISE_GATE  120   // higher threshold for noise gate

// ── MAX98357A Amplifier (I2S_NUM_1) ───────────────────────────
#define AMP_BCLK_PIN    33
#define AMP_LRCLK_PIN   32
#define AMP_DIN_PIN     25
#define AMP_PORT        I2S_NUM_1
// FIX: was 8000 — must match sender LR_SAMPLE_RATE=16000
#define AMP_SAMPLE_RATE 16000

// ── ESP-NOW protocol (must match sender firmware) ─────────────
#define ENW_WIFI_CHANNEL    1
#define ENW_AP_SSID         "TRACKER_ENW"
#define ENW_AP_PSK          "12345678"
#define ENW_CHUNK_SIZE      200
#define ENW_ACK_TIMEOUT_MS  400

// Packet type IDs — identical to sender ESPNow_Transfer.h
#define PKT_HANDSHAKE       0x01
#define PKT_HANDSHAKE_ACK   0x02
#define PKT_FILE_META       0x03
#define PKT_FILE_META_ACK   0x04
#define PKT_CHUNK           0x05
#define PKT_CHUNK_ACK       0x06
#define PKT_DONE            0x07
#define PKT_DONE_ACK        0x08
#define PKT_ABORT           0x09

// ── Notification buzzer (optional, set to -1 to disable) ──────
#define BUZZER_PIN      -1
#define NOTIFY_FREQ_HZ  1200
#define NOTIFY_BEEPS    3

// ── Display refresh ───────────────────────────────────────────
#define DISPLAY_FPS     20
#define DISPLAY_TICK_MS (1000 / DISPLAY_FPS)

// ── Audio recording ───────────────────────────────────────────
#define MAX_RECORD_MS   30000
#define REC_WRITE_BUF   512

// ── Inbox file prefix ─────────────────────────────────────────
#define WT_FILE_PREFIX  "WT_MSG_"

// ── Live Radio (ESP-NOW real-time voice, half-duplex PTT) ─────
// Audio format: 16-bit Signed Mono PCM @ 16 kHz
// MIC and AMP pins are shared from the sections above.
// Packet payload: 120 samples × 2 bytes = 240 bytes (fits ESP-NOW 250B)
#define LR_DEFAULT_CHANNEL   0       // which channel this device listens on
#define LR_SAMPLE_RATE       16000   // must match MIC_SAMPLE_RATE
#define LR_CHUNK_SAMPLES     120     // samples per ESP-NOW packet
#define LR_RX_TIMEOUT_MS     100     // ms with no packet → return to IDLE
#define LR_TX_MAX_MS         8000    // max continuous TX duration (safety cap)
#define LR_RX_QUEUE_SIZE     32       // ring-buffer depth (LR_Packet entries)

// ── DSP Processing Constants ───────────────────────────────────────
#define GAIN_BOOST            1.5f   // reduced gain boost to limit noise amplification
#define DC_BLOCK_ALPHA        0.995f   // DC blocker pole coefficient
#define LPF_ALPHA             0.50f   // smoother LPF to attenuate high‑frequency noise
#define RMS_SMOOTHING         0.90f    // RMS envelope smoothing
#define NOISE_GATE_THRESHOLD  120      // match higher MIC_NOISE_GATE
#define COMP_THRESHOLD        120      // RMS level where compression starts
#define COMP_RATIO            4.0f     // Compression ratio (4:1)
#define COMP_KNEE             0.5f     // Soft‑knee width (0‑1)

#endif // CONFIG_H