#ifndef ESPNOW_H
#define ESPNOW_H

// ════════════════════════════════════════════════════════════════
//  ESPNow.h — ESP-NOW receiver (compatible with sender firmware)
//  Protocol: HANDSHAKE → FILE_META → CHUNK(s) → DONE
//  All ACK responses are sent back to the sender MAC.
//  Received WT_MSG_*.pcm files are stored to SD: /WALKIE/
//  After DONE: notifyNewMessage() is called.
// ════════════════════════════════════════════════════════════════

#include "Config.h"
#include <esp_now.h>
#include <WiFi.h>
#include <SD.h>

// ── Receiver transfer states ──────────────────────────────────
#define RX_IDLE       0
#define RX_META       1    // received FILE_META, waiting for chunks
#define RX_CHUNKS     2    // receiving chunks
#define RX_DONE       3    // transfer complete
#define RX_ERROR      4

// ── Exposed state ──────────────────────────────────────────────
extern uint8_t  rxState;
extern char     rxFileName[48];     // current incoming filename
extern uint32_t rxBytesTotal;       // from FILE_META
extern uint32_t rxBytesRx;         // bytes written so far
extern uint8_t  rxProgress;        // 0-100
extern uint32_t rxStartMs;
extern uint8_t  rxSenderMAC[6];
extern bool     rxNewMessage;       // set true on DONE; cleared by UI
extern char     rxLastFile[48];     // path of most recently completed file

// ── Lifecycle ─────────────────────────────────────────────────
void enwReceiverInit();    // WiFi AP+STA + ESP-NOW + callbacks
void enwReceiverTick();    // call from loop(); handles timeouts

// ── Outgoing (receiver can also send WT messages back) ────────
void enwSenderStart(const char* filePath);
void enwSenderTick();

// ── Peer state ────────────────────────────────────────────────
extern bool enwPeerSeen;      // true if we received at least one packet

// ── Internal (do not call directly) ──────────────────────────
void enwOnRecvNew(const esp_now_recv_info_t* recv_info,
                  const uint8_t* data, int len);
void enwOnSent(const uint8_t* mac, esp_now_send_status_t status);

#endif // ESPNOW_H
