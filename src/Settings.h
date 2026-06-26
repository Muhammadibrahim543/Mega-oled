#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

extern uint8_t  setVolume;    // 0 to 100
extern uint8_t  setMicGain;   // 0 to 200 (100 = 1.0x, 180 = 1.8x)
extern uint8_t  setScreenSav; // 0=Off, 1=1min, 2=5min

void settingsInit();
void settingsSave();

#endif // SETTINGS_H
