#ifndef SDCARD_H
#define SDCARD_H

// ════════════════════════════════════════════════════════════════
//  SDCard.h — SD card init, inbox scan, file management
// ════════════════════════════════════════════════════════════════

#include "Config.h"
#include <SD.h>
#include <SPI.h>

extern bool    sdReady;
extern uint8_t sdFileCount;
extern char    sdFiles[SD_MAX_FILES][48];

bool    sdInit();
void    sdScanFolder(const char* folderPath);
uint16_t sdNextMsgNum();
bool    sdFileExists(const char* path);
void    sdDeleteFile(const char* path);
uint32_t sdFileSize(const char* path);

#endif // SDCARD_H
