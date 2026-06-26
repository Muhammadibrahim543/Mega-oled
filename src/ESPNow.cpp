// ════════════════════════════════════════════════════════════════
//  ESPNow.cpp — ESP-NOW receiver  (Walkie-Talkie Receiver ESP32)
//  Compatible with sender: channel 1, AP_SSID "TRACKER_ENW"
//  Protocol IDs 0x01-0x09 as defined in sender ESPNow_Transfer.h
// ════════════════════════════════════════════════════════════════

#include "ESPNow.h"
#include "SDCard.h"
#include "Notify.h"
#include "LiveRadio.h"   // Live Radio packet interception
#include <Arduino.h>
#include <string.h>

// ── State ─────────────────────────────────────────────────────
uint8_t  rxState       = RX_IDLE;
char     rxFileName[48]  = "";
uint32_t rxBytesTotal  = 0;
uint32_t rxBytesRx     = 0;
uint8_t  rxProgress    = 0;
uint32_t rxStartMs     = 0;
uint8_t  rxSenderMAC[6]= {0};
bool     rxNewMessage  = false;
char     rxLastFile[48]  = "";
bool     enwPeerSeen   = false;

// ── Outgoing sender state ──────────────────────────────────────
static uint8_t  _txState      = 0;  // 0=idle 1=scanning 2=sending 3=done 4=err
static char     _txFilePath[48] = "";
static File     _txFile;
static uint32_t _txChunkIdx   = 0;
static uint32_t _txTotalChunks = 0;
static uint32_t _txAckWaitMs  = 0;
static uint8_t  _txRetry      = 0;
static bool     _txAckRx      = false;
static bool     _txSentOk     = false;
static uint8_t  _txPeerMAC[6] = {0};
static bool     _txPeerAdded  = false;
static bool     _txDoneAckRx  = false;
static uint32_t _txScanStartMs = 0;

// ── Receive file handle ────────────────────────────────────────
static File     _rxFile;
static uint32_t _rxExpectedChunk = 0;
static uint32_t _rxLastChunkMs   = 0;
#define RX_TIMEOUT_MS 5000

// ════════════════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════════════════
static void sendPkt(const uint8_t* mac, const uint8_t* data, int len) {
    // Register peer if not already added
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t pi = {};
        memcpy(pi.peer_addr, mac, 6);
        pi.channel = ENW_WIFI_CHANNEL;
        pi.encrypt = false;
        esp_now_add_peer(&pi);
    }
    esp_now_send(mac, data, len);
}

static void sendAck(const uint8_t* mac, uint8_t pktType,
                    uint32_t chunkIdx = 0) {
    uint8_t buf[5];
    buf[0] = pktType;
    buf[1] = (uint8_t)(chunkIdx & 0xFF);
    buf[2] = (uint8_t)((chunkIdx >> 8) & 0xFF);
    buf[3] = (uint8_t)((chunkIdx >> 16) & 0xFF);
    buf[4] = 0;
    sendPkt(mac, buf, 5);
}

// ════════════════════════════════════════════════════════════════
//  RECEIVE CALLBACK — ESP32 Board Manager v3.0 signature
// ════════════════════════════════════════════════════════════════
void enwOnRecvNew(const esp_now_recv_info_t* recv_info,
                  const uint8_t* data, int len) {
    if (recv_info == NULL || data == NULL) return;

    // Extract MAC from recv_info
    const uint8_t* mac = recv_info->src_addr;
    if (mac == NULL) return;
    if (len < 1) return;

    // ── Live Radio intercept ─────────────────────────────────
    // LR_Packets carry magic byte 0xAD and are handled entirely
    // by LiveRadio.cpp.  They must NOT fall through to the
    // file-transfer switch-case below.
    if (lrIsLRPacket(data, len)) {
        lrHandlePacket(data, len);
        return;
    }

    enwPeerSeen = true;
    uint8_t pktType = data[0];

    switch (pktType) {

    case PKT_HANDSHAKE: {
        // Remember sender MAC
        memcpy(rxSenderMAC, mac, 6);
        sendAck(mac, PKT_HANDSHAKE_ACK);
        Serial.printf("[ENW-RX] HANDSHAKE from %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        break;
    }

    case PKT_FILE_META: {
        if (len < 37) break; // 1 + 32 + 4
        // Parse: byte1..32 = filename, bytes 33..36 = size
        char fname[33] = {0};
        memcpy(fname, data + 1, 32);
        fname[32] = '\0';
        uint32_t fsize = 0;
        memcpy(&fsize, data + 33, 4);

        // Force reset any leftover state from previous transfer
        if (_rxFile) _rxFile.close();
        rxState = RX_IDLE;

        memcpy(rxSenderMAC, mac, 6);
        strncpy(rxFileName, fname, 47);
        rxBytesTotal    = fsize;
        rxBytesRx       = 0;
        rxProgress      = 0;
        _rxExpectedChunk = 0;
        rxStartMs       = millis();
        _rxLastChunkMs  = millis();

        // Build full save path in INBOX folder
        char savePath[64];
        const char* bn = strrchr(fname, '/');
        bn = bn ? bn + 1 : fname;
        snprintf(savePath, sizeof(savePath), "%s/%s", SD_PATH_INBOX, bn);

        // Ensure .pcm extension
        if (strstr(savePath, ".pcm") == NULL) {
            strncat(savePath, ".pcm", sizeof(savePath) - strlen(savePath) - 1);
        }

        strncpy(rxLastFile, savePath, 47);

        if (_rxFile) _rxFile.close();
        _rxFile = SD.open(savePath, FILE_WRITE);
        rxState = _rxFile ? RX_CHUNKS : RX_ERROR;

        sendAck(mac, PKT_FILE_META_ACK);
        Serial.printf("[ENW-RX] META: %s  %lu bytes  state=%d\n",
                      savePath, fsize, rxState);
        break;
    }

    case PKT_CHUNK: {
        if (rxState != RX_CHUNKS) break;
        if (len < 5) break;

        uint32_t idx = (uint32_t)data[1]
                     | ((uint32_t)data[2] << 8)
                     | ((uint32_t)data[3] << 16);
        const uint8_t* payload = data + 4;
        int payLen  = len - 4;

        _rxLastChunkMs = millis();

        // Write chunk (accept duplicate — idempotent)
        if (idx == _rxExpectedChunk) {
            if (_rxFile && payLen > 0) {
                _rxFile.write(payload, payLen);
                rxBytesRx += payLen;
                _rxExpectedChunk++;
                if (rxBytesTotal > 0)
                    rxProgress = (uint8_t)((uint64_t)rxBytesRx * 100 / rxBytesTotal);
            }
        }
        // Always ACK (even duplicates, to unblock sender retry)
        sendAck(mac, PKT_CHUNK_ACK, idx);
        break;
    }

    case PKT_DONE: {
        if (_rxFile) {
            _rxFile.flush();
            _rxFile.close();
        }
        rxProgress   = 100;
        rxState      = RX_DONE;
        rxNewMessage = true;
        sendAck(mac, PKT_DONE_ACK);

        Serial.printf("[ENW-RX] DONE: %s  %lu bytes\n",
                      rxLastFile, rxBytesRx);

        // Rescan SD inbox
        sdScanFolder(SD_PATH_INBOX);

        // Fire notification
        notifyNewMessage(rxLastFile);
        break;
    }

    case PKT_ABORT: {
        if (_rxFile) _rxFile.close();
        rxState = RX_ERROR;
        Serial.println("[ENW-RX] ABORT");
        break;
    }

    // ── ACK packets for outgoing transfers ──────────────────
    case PKT_HANDSHAKE_ACK: {
        // Sender confirmed our handshake — let tick() open file + send META
        memcpy(_txPeerMAC, mac, 6);
        _txAckRx = true;  // _txState stays 1; tick() will open file and go to state 2
        break;
    }
    case PKT_FILE_META_ACK: {
        _txAckRx   = true;
        break;
    }
    case PKT_CHUNK_ACK: {
        uint32_t idx = (uint32_t)data[1]
                     | ((uint32_t)data[2] << 8)
                     | ((uint32_t)data[3] << 16);
        // _txChunkIdx already incremented after send, so ACK for last sent = _txChunkIdx-1
        if (_txChunkIdx > 0 && idx == _txChunkIdx - 1) _txAckRx = true;
        break;
    }
    case PKT_DONE_ACK: {
        _txDoneAckRx = true;
        break;
    }

    default:
        break;
    }
}

// ════════════════════════════════════════════════════════════════
//  SEND CALLBACK
// ════════════════════════════════════════════════════════════════
void enwOnSent(const uint8_t* mac, esp_now_send_status_t status) {
    _txSentOk = (status == ESP_NOW_SEND_SUCCESS);
}

// ── Beacon state (receiver advertises itself to sender) ────────
static uint32_t _beaconLastMs = 0;
#define BEACON_INTERVAL_MS 3000   // broadcast every 3s until peer seen

// ════════════════════════════════════════════════════════════════
//  INIT
// ════════════════════════════════════════════════════════════════
void enwReceiverInit() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ENW_AP_SSID, ENW_AP_PSK, ENW_WIFI_CHANNEL);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ENW] esp_now_init FAILED");
        return;
    }

    esp_now_register_recv_cb(enwOnRecvNew);
    esp_now_register_send_cb(enwOnSent);
    Serial.printf("[ENW] Receiver ready  ch=%d\n", ENW_WIFI_CHANNEL);

    // Send initial broadcast so sender learns our MAC immediately
    _beaconLastMs = 0;
}

// ════════════════════════════════════════════════════════════════
//  SEND BEACON — broadcast HANDSHAKE so sender learns our MAC
// ════════════════════════════════════════════════════════════════
static void sendBeacon() {
    uint8_t broadcastMAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t pkt[1] = {PKT_HANDSHAKE};
    sendPkt(broadcastMAC, pkt, 1);
    Serial.println("[ENW-RX] Beacon broadcast sent");
}

// ════════════════════════════════════════════════════════════════
//  RECEIVER TICK (timeout watchdog + beacon)
// ════════════════════════════════════════════════════════════════
void enwReceiverTick() {
    uint32_t now = millis();

    // Periodically broadcast our presence so sender learns our MAC
    // Stop beaconing once peer is seen (they already know us)
    if (!enwPeerSeen && (now - _beaconLastMs >= BEACON_INTERVAL_MS)) {
        _beaconLastMs = now;
        sendBeacon();
    }

    if (rxState == RX_CHUNKS) {
        if (now - _rxLastChunkMs > RX_TIMEOUT_MS) {
            Serial.println("[ENW-RX] Timeout — aborting");
            if (_rxFile) _rxFile.close();
            rxState = RX_IDLE;  // Go directly to IDLE so next transfer is not blocked
        }
    }
    // Reset DONE/ERROR state so next incoming transfer is accepted
    if (rxState == RX_DONE || rxState == RX_ERROR) {
        static uint32_t doneMs = 0;
        if (doneMs == 0) doneMs = now;
        if (now - doneMs > 1500) {  // was 3000ms — reduced to unblock faster
            rxState = RX_IDLE;
            doneMs  = 0;
        }
    }
}

// ════════════════════════════════════════════════════════════════
//  OUTGOING SENDER (receiver replying to sender device)
// ════════════════════════════════════════════════════════════════
void enwSenderStart(const char* filePath) {
    if (_txState != 0 && _txState != 3 && _txState != 4) return;
    strncpy(_txFilePath, filePath, 47);
    _txChunkIdx    = 0;
    _txRetry       = 0;
    _txAckRx       = false;
    _txDoneAckRx   = false;
    _txPeerAdded   = false;
    _txScanStartMs = millis();
    _txState       = 1;  // SCANNING — send HANDSHAKE broadcast

    // Send HANDSHAKE to sender MAC if known, else broadcast
    uint8_t broadcastMAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t dest[6];
    bool hasPeer = false;
    for (int i = 0; i < 6; i++) if (rxSenderMAC[i]) { hasPeer = true; break; }
    memcpy(dest, hasPeer ? rxSenderMAC : broadcastMAC, 6);

    uint8_t pkt[1] = {PKT_HANDSHAKE};
    sendPkt(dest, pkt, 1);
    _txAckWaitMs = millis();
    Serial.printf("[ENW-TX] Start: %s\n", filePath);
}

void enwSenderTick() {
    if (_txState == 0 || _txState == 3 || _txState == 4) return;

    uint32_t now = millis();

    // ── SCANNING: wait for HANDSHAKE_ACK ──────────────────────
    if (_txState == 1) {
        if (_txAckRx) {
            // Peer responded; send FILE_META
            _txAckRx   = false;
            // Open file
            if (_txFile) _txFile.close();
            _txFile = SD.open(_txFilePath, FILE_READ);
            if (!_txFile) { _txState = 4; return; }
            uint32_t fsize = _txFile.size();
            _txTotalChunks = (fsize + ENW_CHUNK_SIZE - 1) / ENW_CHUNK_SIZE;

            // Build FILE_META packet
            uint8_t metaPkt[37] = {0};
            metaPkt[0] = PKT_FILE_META;
            const char* bn = strrchr(_txFilePath, '/');
            bn = bn ? bn + 1 : _txFilePath;
            strncpy((char*)(metaPkt + 1), bn, 32);
            memcpy(metaPkt + 33, &fsize, 4);
            sendPkt(_txPeerMAC, metaPkt, 37);
            _txAckWaitMs = now;
            _txRetry = 0;
            _txState = 2;
            Serial.printf("[ENW-TX] META sent (%lu bytes, %lu chunks)\n",
                          fsize, _txTotalChunks);
            return;
        }
        if (now - _txScanStartMs > 5000) { _txState = 4; return; }
        // Retry handshake every 800ms
        if (now - _txAckWaitMs > 800) {
            uint8_t pkt[1] = {PKT_HANDSHAKE};
            bool hasPeer = false;
            for (int i = 0; i < 6; i++) if (rxSenderMAC[i]) { hasPeer = true; break; }
            uint8_t dest[6];
            if (hasPeer) memcpy(dest, rxSenderMAC, 6);
            else memset(dest, 0xFF, 6);
            sendPkt(dest, pkt, 1);
            _txAckWaitMs = now;
        }
        return;
    }

    // ── SENDING: send chunks ───────────────────────────────────
    if (_txState == 2) {
        bool fileExhausted = !_txFile ||
                             (_txFile.size() > 0 && _txFile.position() >= _txFile.size()) ||
                             (_txChunkIdx > 0 && _txChunkIdx >= _txTotalChunks);
        if (fileExhausted) {
            // All chunks sent — send DONE
            uint8_t donePkt[1] = {PKT_DONE};
            sendPkt(_txPeerMAC, donePkt, 1);
            _txAckWaitMs = now;
            _txRetry     = 0;
            _txState = 3;
            if (_txFile) _txFile.close();
            Serial.println("[ENW-TX] DONE sent");
            return;
        }

        // Send next chunk if previous was ACKed (or first chunk)
        if (_txAckRx || _txChunkIdx == 0) {
            _txAckRx = false;

            uint8_t chunkPkt[4 + ENW_CHUNK_SIZE];
            chunkPkt[0] = PKT_CHUNK;
            chunkPkt[1] = (uint8_t)(_txChunkIdx & 0xFF);
            chunkPkt[2] = (uint8_t)((_txChunkIdx >> 8) & 0xFF);
            chunkPkt[3] = (uint8_t)((_txChunkIdx >> 16) & 0xFF);
            int rd = _txFile.read(chunkPkt + 4, ENW_CHUNK_SIZE);
            if (rd <= 0) return;
            sendPkt(_txPeerMAC, chunkPkt, 4 + rd);
            _txAckWaitMs = now;
            _txRetry     = 0;
            _txChunkIdx++;
        } else {
            // Wait for ACK
            if (now - _txAckWaitMs > ENW_ACK_TIMEOUT_MS) {
                if (++_txRetry >= 5) { _txState = 4; return; }
                // Seek back and resend last chunk
                uint32_t seekPos = (_txChunkIdx > 0)
                    ? (_txChunkIdx - 1) * ENW_CHUNK_SIZE : 0;
                _txFile.seek(seekPos);
                _txAckRx = false; // force resend
                _txAckWaitMs = now;
                Serial.printf("[ENW-TX] retry chunk %lu\n", _txChunkIdx - 1);
            }
        }
    }
}

uint8_t enwSenderState() { return _txState; }
uint8_t enwSenderProgress() {
    if (_txTotalChunks == 0) return 0;
    return (uint8_t)((_txChunkIdx * 100) / _txTotalChunks);
}
