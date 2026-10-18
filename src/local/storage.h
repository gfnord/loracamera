// microSD card on the base station: every received picture/clip is saved here.
#pragma once

#include <Arduino.h>

// Mount the card (FAT32) and find the next free file number. Safe to call
// without a card: it logs and returns false, and storageSave() retries later.
bool storageBegin();

// Save one received file as /loracam/NNNNNN.<ext>. Remounts the card if it
// was removed or not present at boot. Returns false if nothing was written.
bool storageSave(const uint8_t *data, size_t len, const char *ext);
