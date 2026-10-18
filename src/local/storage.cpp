// Only built into firmware with sdcard = 1, so builds without a card don't link the SD library.
#if defined(LORACAMERA_SDCARD) && LORACAMERA_SDCARD
#include "storage.h"

#include <SD.h>
#include <SPI.h>

// microSD breakout on its own SPI bus (HSPI); the LoRa radio owns the default SPI bus.
// Free GPIOs on header J3 of the V4-R8 (also on the GNSS connector: leave that empty).
#define SD_CS 4    // J3 pin 15
#define SD_SCK 39  // J3 pin 10
#define SD_MISO 38 // J3 pin 11
#define SD_MOSI 41 // J3 pin 8
#define SD_SPI_HZ 4000000

static const char *DIR_NAME = "/loracam";

static SPIClass sdSPI(HSPI);
static bool mounted = false;
static uint32_t nextIndex = 1;

static void unmount() {
	SD.end();
	sdSPI.end();
	mounted = false;
}

// Highest NNNNNN in /loracam, so numbering continues across reboots.
static uint32_t highestIndex() {
	uint32_t highest = 0;
	File dir = SD.open(DIR_NAME);
	if (!dir) return 0;
	for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
		const char *name = f.name();
		const char *slash = strrchr(name, '/');
		if (slash) name = slash + 1;
		uint32_t n = strtoul(name, nullptr, 10);
		if (n > highest) highest = n;
		f.close();
	}
	dir.close();
	return highest;
}

static bool mount() {
	if (mounted) return true;
	sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
	if (!SD.begin(SD_CS, sdSPI, SD_SPI_HZ)) {
		Serial.printf("SD card not found (needs a FAT32 card in the breakout)\n");
		unmount();
		return false;
	}
	if (!SD.exists(DIR_NAME) && !SD.mkdir(DIR_NAME)) {
		Serial.printf("SD card: can't create %s\n", DIR_NAME);
		unmount();
		return false;
	}
	mounted = true;
	nextIndex = highestIndex() + 1;
	Serial.printf("SD card: %llu MB free, next file %06lu\n",
		(SD.totalBytes() - SD.usedBytes()) / (1024 * 1024), (unsigned long)nextIndex);
	return true;
}

bool storageBegin() { return mount(); }

bool storageSave(const uint8_t *data, size_t len, const char *ext) {
	if (!mount()) return false;
	if (SD.totalBytes() - SD.usedBytes() < len) {
		Serial.printf("SD card full, %u-byte file not saved\n", (unsigned)len);
		return false;
	}
	char path[32];
	snprintf(path, sizeof(path), "%s/%06lu.%s", DIR_NAME, (unsigned long)nextIndex, ext);
	File f = SD.open(path, FILE_WRITE);
	if (!f) {
		Serial.printf("SD card: can't open %s, remounting next time\n", path);
		unmount();
		return false;
	}
	size_t written = f.write(data, len);
	f.close();
	if (written != len) {
		Serial.printf("SD card: wrote %u of %u bytes to %s, remounting next time\n",
			(unsigned)written, (unsigned)len, path);
		unmount();
		return false;
	}
	nextIndex++;
	Serial.printf("Saved %s (%u bytes)\n", path, (unsigned)len);
	return true;
}


#endif