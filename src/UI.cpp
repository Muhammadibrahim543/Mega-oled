// ════════════════════════════════════════════════════════════════
//  UI.cpp — App state machine  v2.0 (Clean Walkie-Talkie)
//  Walkie-Talkie Receiver (ESP32)
// ════════════════════════════════════════════════════════════════

#include "UI.h"
#include "Display.h"
#include "Buttons.h"
#include "Audio.h"
#include "ESPNow.h"
#include "SDCard.h"
#include "Notify.h"
#include "LiveRadio.h"
#include <Arduino.h>
#include <string.h>
#include <math.h>

#include "Settings.h"

uint8_t uiPage     = PAGE_SPLASH;
uint8_t uiPrevPage = PAGE_MODE_SELECT;

// ── Mode select state ─────────────────────────────────────────
static uint8_t _modeSelIdx = 0;   // 0 = File Transfer, 1 = Walkie-Talkie, 2 = Live Radio
static float   _modeScrollY = 0.0f;

// ── Inbox state ───────────────────────────────────────────────
static uint8_t  _inboxSel   = 0;
static uint16_t _msgNum     = 1;

// ── Smooth scroll ─────────────────────────────────────────────
static float   _scrollY    = 0.0f;
static uint8_t _scrollLock = 0;
static const float SCROLL_EASE = 0.18f;
static const float SCROLL_SNAP = 0.012f;

// ── Sending state ─────────────────────────────────────────────
extern uint8_t enwSenderState();
extern uint8_t enwSenderProgress();

// ════════════════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════════════════
static inline bool scrollSettled() {
    float d = _scrollY - (float)_inboxSel;
    return d > -SCROLL_SNAP && d < SCROLL_SNAP;
}
static void scrollTo(uint8_t s) { _inboxSel = s; _scrollLock = 1; }

// ════════════════════════════════════════════════════════════════
//  INIT
// ════════════════════════════════════════════════════════════════
void uiInit() {
    uiPage      = PAGE_SPLASH;        // Boot into splash screen
    uiPrevPage  = PAGE_MODE_SELECT;
    _modeSelIdx = 0;                  // Default cursor: File Transfer
    _modeScrollY = 0.0f;
    _inboxSel   = 0;
    _scrollY    = 0.0f;
    _scrollLock = 0;
    _msgNum     = sdNextMsgNum();
    sdScanFolder(SD_PATH_INBOX);
}

// ════════════════════════════════════════════════════════════════
//  BUTTON ROUTING
// ════════════════════════════════════════════════════════════════
void uiHandleBtn(uint8_t ev) {
    if (ev == BTN_NONE) return;

    // ── Global: notification dismisses on any button ──────────
    if (notifyActive) {
        notifyDismiss();
        uiPage = PAGE_STANDBY;    // Return to WT standby after notify
        return;
    }

    // ── Global: two-button menu shortcut ──────────────────────
    if (ev == BTN_GLOBAL_MENU && uiPage != PAGE_SPLASH) {
        if (uiPage != PAGE_MODE_SELECT) {
            // Stop any ongoing audio/radio actions before switching
            if (audioState == AUDIO_PLAYING) audioStopPlay();
            if (audioState == AUDIO_RECORDING) {
                audioStopRecord();
                // We're aborting recording, delete the file
                char path[64];
                snprintf(path, sizeof(path), "%s/%s%04d.pcm", SD_PATH_SENT, WT_FILE_PREFIX, _msgNum);
                sdDeleteFile(path);
            }
            if (lrState == LR_TX) lrPttUp();
            lrAmpStop();
            lrMicStop();

            _modeSelIdx = 0;
            uiPage = PAGE_MODE_SELECT;
        }
        return;
    }

    // ── PAGE_MODE_SELECT ──────────────────────────────────────
    if (uiPage == PAGE_MODE_SELECT) {
        if (ev == BTN_PREV_SHORT) {
            _modeSelIdx = (_modeSelIdx + 4) % 5;
        } else if (ev == BTN_NEXT_SHORT) {
            _modeSelIdx++;
            if (_modeSelIdx > 4) _modeSelIdx = 4;
        } else if (ev == BTN_SEL_SHORT) {
            if (_modeSelIdx == 0) {
                uiPage = PAGE_FILE_TRANSFER;
            } else if (_modeSelIdx == 1) {
                uiPage = PAGE_STANDBY;
            } else if (_modeSelIdx == 2) {
                uiPage = PAGE_LIVE_RADIO;
            } else if (_modeSelIdx == 3) {
                uiPage = PAGE_FILE_BROWSER;
            } else if (_modeSelIdx == 4) {
                uiPage = PAGE_SETTINGS;
                // Reuse _modeSelIdx for settings row selection
                _modeSelIdx = 0; 
            }
        }
        return;   // Block all other buttons while on mode select
    }

    // ── PAGE_FILE_TRANSFER / PAGE_FILE_BROWSER ───────────────
    if (uiPage == PAGE_FILE_TRANSFER || uiPage == PAGE_FILE_BROWSER) {
        if (ev == BTN_PREV_SHORT || ev == BTN_PREV_LONG) {
            _modeSelIdx = (uiPage == PAGE_FILE_TRANSFER) ? 0 : 3;
            uiPage = PAGE_MODE_SELECT;
        }
        return;
    }

    // ── PAGE_SETTINGS ─────────────────────────────────────────
    if (uiPage == PAGE_SETTINGS) {
        if (ev == BTN_SEL_SHORT) {
            // Save and exit
            settingsSave();
            _modeSelIdx = 4;
            uiPage = PAGE_MODE_SELECT;
        } else if (ev == BTN_PREV_SHORT) {
            // Decrease value
            if (_modeSelIdx == 0 && setVolume >= 10) setVolume -= 10;
            else if (_modeSelIdx == 1 && setMicGain >= 10) setMicGain -= 10;
            else if (_modeSelIdx == 2 && setScreenSav > 0) setScreenSav--;
        } else if (ev == BTN_NEXT_SHORT) {
            // Increase value
            if (_modeSelIdx == 0 && setVolume <= 90) setVolume += 10;
            else if (_modeSelIdx == 1 && setMicGain <= 190) setMicGain += 10;
            else if (_modeSelIdx == 2 && setScreenSav < 2) setScreenSav++;
        } else if (ev == BTN_PREV_LONG) {
            // Move up
            if (_modeSelIdx > 0) _modeSelIdx--;
        } else if (ev == BTN_NEXT_LONG) {
            // Move down
            if (_modeSelIdx < 2) _modeSelIdx++;
        }
        return;
    }

    // ── PAGE_STANDBY ──────────────────────────────────────────
    if (uiPage == PAGE_STANDBY) {
        if (ev == BTN_NEXT_SHORT || ev == BTN_SEL_SHORT) {
            sdScanFolder(SD_PATH_INBOX);
            _inboxSel  = 0;
            _scrollY   = 0.0f;
            _scrollLock = 0;
            uiPage = PAGE_INBOX;

        } else if (ev == BTN_PREV_LONG) {
            // Return to Mode Selection screen (switch modes)
            uiPage = PAGE_MODE_SELECT;

        } else if (ev == BTN_PTT_DOWN) {
            if (audioState == AUDIO_IDLE && sdReady) {
                char path[64];
                _msgNum = sdNextMsgNum();
                snprintf(path, sizeof(path),
                         "%s/%s%04d.pcm", SD_PATH_SENT, WT_FILE_PREFIX, _msgNum);
                if (audioStartRecord(path)) {
                    Serial.printf("[UI] ✓ Recording started: %s\n", path);
                    uiPrevPage = PAGE_STANDBY;
                    uiPage     = PAGE_RECORDING;
                } else {
                    Serial.println("[UI] ✗ audioStartRecord failed");
                    Serial.println("     → Is SD card ready? Check sdReady flag");
                    Serial.println("     → Is mic I2S already in use?");
                }
            }
        }
    }

    // ── PAGE_INBOX ────────────────────────────────────────────
    else if (uiPage == PAGE_INBOX) {
        if (ev == BTN_PREV_SHORT) {
            if (sdFileCount > 0) {
                if (_scrollLock && !scrollSettled()) return;
                if (audioState == AUDIO_PLAYING) audioStopPlay();
                scrollTo((_inboxSel + sdFileCount - 1) % sdFileCount);
            }
        } else if (ev == BTN_NEXT_SHORT) {
            if (sdFileCount > 0) {
                if (_scrollLock && !scrollSettled()) return;
                if (audioState == AUDIO_PLAYING) audioStopPlay();
                scrollTo((_inboxSel + 1) % sdFileCount);
            } else {
                uiPage = PAGE_STANDBY;
            }
        } else if (ev == BTN_SEL_SHORT) {
            if (!scrollSettled()) return;
            if (sdFileCount > 0 && _inboxSel < sdFileCount) {
                if (audioState == AUDIO_PLAYING) {
                    audioStopPlay();
                } else {
                    uint8_t fi = (sdFileCount - 1) - _inboxSel;
                    audioStartPlay(sdFiles[fi]);
                }
            }
        } else if (ev == BTN_SEL_LONG) {
            if (!scrollSettled()) return;
            if (sdFileCount > 0 && _inboxSel < sdFileCount
                && audioState == AUDIO_IDLE) {
                uint8_t fi = (sdFileCount - 1) - _inboxSel;
                enwSenderStart(sdFiles[fi]);
                uiPage = PAGE_SENDING;
            }
        } else if (ev == BTN_PREV_LONG) {
            if (audioState == AUDIO_PLAYING) audioStopPlay();
            uiPage = PAGE_STANDBY;
        } else if (ev == BTN_PTT_DOWN) {
            if (audioState == AUDIO_IDLE && sdReady) {
                if (audioState == AUDIO_PLAYING) audioStopPlay();
                char path[64];
                _msgNum = sdNextMsgNum();
                snprintf(path, sizeof(path),
                         "%s/%s%04d.pcm", SD_PATH_SENT, WT_FILE_PREFIX, _msgNum);
                if (audioStartRecord(path)) {
                    Serial.printf("[UI] ✓ Recording started from inbox: %s\n", path);
                    uiPrevPage = PAGE_INBOX;
                    uiPage     = PAGE_RECORDING;
                } else {
                    Serial.println("[UI] ✗ audioStartRecord failed (from inbox)");
                }
            }
        }
    }

    // ── PAGE_RECORDING ────────────────────────────────────────
    else if (uiPage == PAGE_RECORDING) {
        if (ev == BTN_PTT_UP || ev == BTN_SEL_SHORT) {
            audioStopRecord();
            if (audioBytes < 256) {
                Serial.printf("[UI] Recording too short (%lu bytes < 256) — discarded\n",
                              audioBytes);
                char path[64];
                snprintf(path, sizeof(path),
                         "%s/%s%04d.pcm", SD_PATH_SENT, WT_FILE_PREFIX, _msgNum);
                sdDeleteFile(path);
                uiPage = uiPrevPage;
            } else {
                Serial.printf("[UI] ✓ Recording OK %lu bytes → sending\n", audioBytes);
                char path[64];
                snprintf(path, sizeof(path),
                         "%s/%s%04d.pcm", SD_PATH_SENT, WT_FILE_PREFIX, _msgNum);
                enwSenderStart(path);
                uiPage = PAGE_SENDING;
                _msgNum++;
            }
            sdScanFolder(SD_PATH_INBOX);
        }
    }

    // ── PAGE_SENDING ──────────────────────────────────────────
    else if (uiPage == PAGE_SENDING) {
        if (ev == BTN_SEL_SHORT || ev == BTN_SEL_LONG) {
            uiPage = PAGE_STANDBY;
        }
    }

    // ── PAGE_LIVE_RADIO ──────────────────────────────────────
    else if (uiPage == PAGE_LIVE_RADIO) {
        if (ev == BTN_PTT_DOWN) {
            // Start transmitting
            lrPttDown();

        } else if (ev == BTN_PTT_UP) {
            // Stop transmitting
            lrPttUp();

        } else if (ev == BTN_PREV_SHORT) {
            // Channel down  (only in IDLE)
            if (lrState == LR_IDLE) {
                uint8_t ch = (lrChannel + LR_CHANNEL_COUNT - 1) % LR_CHANNEL_COUNT;
                lrSetChannel(ch);
            }

        } else if (ev == BTN_NEXT_SHORT) {
            // Channel up  (only in IDLE)
            if (lrState == LR_IDLE) {
                uint8_t ch = (lrChannel + 1) % LR_CHANNEL_COUNT;
                lrSetChannel(ch);
            }

        } else if (ev == BTN_PREV_LONG || ev == BTN_SEL_LONG) {
            // Exit Live Radio — make sure I2S is clean before returning
            if (lrState == LR_TX) lrPttUp();
            lrAmpStop();
            lrMicStop();
            _modeSelIdx = 1;              // Keep cursor on Live Radio
            uiPage = PAGE_MODE_SELECT;    // ← Return to mode selection
        }
    }
}

// ════════════════════════════════════════════════════════════════
//  TICK — draw current page each frame
// ════════════════════════════════════════════════════════════════
void uiTick() {
    uint32_t ms = millis();
    oled.clearDisplay();

    // ── Notification overrides everything ──────────────────────
    if (notifyActive) {
        drawPageNotify(notifyFile, ms - notifyReceivedMs, notifyAnimFrame);
        oled.display();
        return;
    }

    // ── Auto-switch to RECEIVING when transfer starts ──────────
    if (rxState == RX_CHUNKS && uiPage != PAGE_RECEIVING) {
        uiPage = PAGE_RECEIVING;
    }
    if (rxState == RX_IDLE && uiPage == PAGE_RECEIVING) {
        uiPage = PAGE_STANDBY;
    }

    // ── Auto-leave SENDING ─────────────────────────────────────
    if (uiPage == PAGE_SENDING) {
        uint8_t ss = enwSenderState();
        if (ss == 3 || ss == 4) uiPage = PAGE_STANDBY;
    }

    // ── Inbox scroll easing ───────────────────────────────────
    if (uiPage == PAGE_INBOX) {
        float diff = (float)_inboxSel - _scrollY;
        if (fabsf(diff) < SCROLL_SNAP) { _scrollY = (float)_inboxSel; _scrollLock = 0; }
        else                             _scrollY += diff * SCROLL_EASE;
    }

    // ── Splash Screen auto-transition ─────────────────────────
    if (uiPage == PAGE_SPLASH) {
        if (ms > 2000) {
            uiPage = PAGE_MODE_SELECT;
        }
    }

    // ── Dispatch ──────────────────────────────────────────────
    switch (uiPage) {
        case PAGE_SPLASH:
            drawPageSplash(ms);
            break;

        case PAGE_MODE_SELECT: {
            float diff = (float)_modeSelIdx - _modeScrollY;
            if (fabsf(diff) < SCROLL_SNAP) _modeScrollY = (float)_modeSelIdx;
            else _modeScrollY += diff * SCROLL_EASE;
            drawPageModeSelect(_modeSelIdx, _modeScrollY, ms);
            break;
        }

        case PAGE_FILE_TRANSFER:
            drawPageFileTransfer(ms);
            break;

        case PAGE_SETTINGS:
            drawPageSettings(_modeSelIdx, ms);
            break;
            
        case PAGE_FILE_BROWSER:
            drawPageFileBrowser(ms);
            break;

        case PAGE_STANDBY:
            drawPageStandby(sdFileCount, enwPeerSeen, ms);
            break;

        case PAGE_RECEIVING:
            drawPageReceiving(
                rxFileName[0] ? rxFileName : "...",
                rxProgress,
                ms - rxStartMs,
                rxBytesRx,
                rxBytesTotal);
            break;

        case PAGE_INBOX:
            drawPageInbox(
                sdFileCount, _inboxSel, _scrollY,
                audioState == AUDIO_PLAYING,
                audioPlayPosMs, audioPlayTotalMs);
            break;

        case PAGE_RECORDING:
            drawPageRecording(audioElapsedMs, audioBytes, audioRms);
            break;

        case PAGE_SENDING: {
            char path[64];
            snprintf(path, sizeof(path),
                     "%s/%s%04d.pcm", SD_PATH_SENT, WT_FILE_PREFIX,
                     _msgNum > 0 ? _msgNum - 1 : 1);
            drawPageSending(path, enwSenderProgress(), enwSenderState());
            break;
        }

        case PAGE_LIVE_RADIO: {
            uint32_t elapsed = 0;
            if (lrState == LR_TX && lrTxStartMs > 0)
                elapsed = ms - lrTxStartMs;
            else if (lrState == LR_RX && lrLastPacketMs > 0)
                elapsed = ms - lrLastPacketMs;
            drawPageLiveRadio(
                lrState, lrChannel, elapsed,
                (lrState == LR_TX) ? lrTxPacketCount : lrRxPacketCount,
                lrLastSample, ms);
            break;
        }

        default:
            drawPageStandby(sdFileCount, enwPeerSeen, ms);
            break;
    }

    oled.display();
}