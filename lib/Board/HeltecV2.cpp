// Radio for the Heltec V2: SX1276, PA_BOOST output, no front-end switching.
#if defined(LORACAMERA_BOARD_HELTEC_V2)
#include "Board.h"

#include <RadioLib.h>
#include <SPI.h>
#include "driver/gpio.h"

static SX1276 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO0, PIN_LORA_RST, PIN_LORA_DIO1, SPI);

static volatile bool packetReady = false;
static int lastRssi = 0;

static void IRAM_ATTR onPacketReceived() { packetReady = true; }

static bool startReceive() {
	packetReady = false;
	int16_t state = radio.startReceive();
	if (state != RADIOLIB_ERR_NONE) {
		Serial.printf("LoRa startReceive failed: %d\n", state);
		return false;
	}
	return true;
}

bool radioBegin() {
	// Set the level first so NSS doesn't glitch low, then release a hold left by radioSleep().
	pinMode(PIN_LORA_NSS, OUTPUT);
	digitalWrite(PIN_LORA_NSS, HIGH);
	gpio_hold_dis((gpio_num_t)PIN_LORA_NSS);

	SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
	// begin() also enables the CRC.
	int16_t state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD,
	                            LORA_TX_POWER_DBM, LORA_PREAMBLE);
	if (state == RADIOLIB_ERR_NONE) state = radio.setCurrentLimit(LORA_CURRENT_LIMIT_MA);
	if (state != RADIOLIB_ERR_NONE) {
		Serial.printf("LoRa init failed: %d\n", state);
		return false;
	}
	// RX done on DIO0. TX done raises it too; startReceive() clears that flag.
	radio.setPacketReceivedAction(onPacketReceived);
	return startReceive();
}

void radioSleep() {
	int16_t state = radio.sleep();
	if (state != RADIOLIB_ERR_NONE) {
		Serial.printf("LoRa sleep failed: %d\n", state);
	}
	// Keep the chip deselected through deep sleep (GPIO18 is a digital pad: the caller's
	// gpio_deep_sleep_hold_en() makes this hold last through deep sleep).
	digitalWrite(PIN_LORA_NSS, HIGH);
	gpio_hold_en((gpio_num_t)PIN_LORA_NSS);
}

bool radioSend(const uint8_t *data, size_t len) {
	int16_t state = radio.transmit(data, len);
	if (state != RADIOLIB_ERR_NONE) {
		Serial.printf("LoRa transmit failed: %d\n", state);
	}
	return startReceive() && state == RADIOLIB_ERR_NONE;
}

size_t radioReceive(uint8_t *buf, size_t maxLen) {
	if (!packetReady) return 0;
	packetReady = false;

	size_t len = radio.getPacketLength();
	int16_t state = radio.readData(buf, len < maxLen ? len : maxLen);
	if (state != RADIOLIB_ERR_NONE) {
		Serial.printf("LoRa receive error: %d\n", state);
		return 0;
	}
	lastRssi = (int)radio.getRSSI();
	return len;
}

int radioLastRssi() { return lastRssi; }

uint32_t radioTimeOnAirMs(size_t len) { return (radio.getTimeOnAir(len) + 999) / 1000; }

#endif
