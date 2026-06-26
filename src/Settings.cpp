#include "Settings.h"
#include <Preferences.h>

uint8_t setVolume    = 80;   // default 80%
uint8_t setMicGain   = 180;  // default 1.8x
uint8_t setScreenSav = 0;    // default Off

Preferences prefs;

void settingsInit() {
    prefs.begin("walkie", false);
    setVolume    = prefs.getUChar("vol", 80);
    setMicGain   = prefs.getUChar("mic", 180);
    setScreenSav = prefs.getUChar("sav", 0);
}

void settingsSave() {
    prefs.putUChar("vol", setVolume);
    prefs.putUChar("mic", setMicGain);
    prefs.putUChar("sav", setScreenSav);
}
