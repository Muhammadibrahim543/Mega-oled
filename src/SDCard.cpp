// ════════════════════════════════════════════════════════════════
//  SDCard.cpp — SD management  (Walkie-Talkie Receiver)
// ════════════════════════════════════════════════════════════════

#include "SDCard.h"

bool    sdReady    = false;
uint8_t sdFileCount = 0;
char    sdFiles[SD_MAX_FILES][48];

bool sdInit() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS)) {
        Serial.println("[SD] init FAILED");
        sdReady = false;
        return false;
    }
    if (!SD.exists(SD_PATH))         SD.mkdir(SD_PATH);
    if (!SD.exists(SD_PATH_INBOX))   SD.mkdir(SD_PATH_INBOX);
    if (!SD.exists(SD_PATH_SENT))    SD.mkdir(SD_PATH_SENT);
    if (!SD.exists(SD_PATH_FILE_RX)) SD.mkdir(SD_PATH_FILE_RX);
    if (!SD.exists(SD_PATH_LIVE))    SD.mkdir(SD_PATH_LIVE);
    sdReady = true;
    Serial.println("[SD] OK");
    sdScanFolder(SD_PATH_INBOX);
    return true;
}

void sdScanFolder(const char* folderPath) {
    sdFileCount = 0;
    File dir = SD.open(folderPath);
    if (!dir || !dir.isDirectory()) return;
    while (sdFileCount < SD_MAX_FILES) {
        File f = dir.openNextFile();
        if (!f) break;
        const char* n = f.name();
        // Allow .pcm files and any other files in the file browser
        snprintf(sdFiles[sdFileCount], 48, "%s/%s", folderPath, n);
        sdFileCount++;
        f.close();
    }
    dir.close();
    Serial.printf("[SD] Folder scan %s: %d files\n", folderPath, sdFileCount);
}

uint16_t sdNextMsgNum() {
    for (uint16_t i = 1; i <= 9999; i++) {
        char p1[52], p2[52];
        snprintf(p1, sizeof(p1), "%s/%s%04d.pcm", SD_PATH_INBOX, WT_FILE_PREFIX, i);
        snprintf(p2, sizeof(p2), "%s/%s%04d.pcm", SD_PATH_SENT,  WT_FILE_PREFIX, i);
        if (!SD.exists(p1) && !SD.exists(p2)) return i;
    }
    return 1;
}

bool sdFileExists(const char* path) { return SD.exists(path); }

void sdDeleteFile(const char* path) { SD.remove(path); }

uint32_t sdFileSize(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t sz = f.size();
    f.close();
    return sz;
}
