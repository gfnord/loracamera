// Radio for the Heltec V4-R8: SX1262 behind the KCT8103L front end.
#if defined(LORACAMERA_BOARD_HELTEC_V4R8)
#include "Board.h"

#include <RadioLib.h>
#include <SPI.h>
#include "driver/gpio.h"

static SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY, SPI);

static volatile bool packetReady = false;
static int lastRssi = 0;

static void IRAM_ATTR onPacketReceived() { packetReady = true; }

// Drive pin as an output at level, then release a hold left by radioSleep(). The level
// is set first so the pin doesn't glitch to its reset state.
static void outputUnheld(uint8_t pin, uint8_t level) {
	pinMode(pin, OUTPUT);
	digitalWrite(pin, level);
	gpio_hold_dis((gpio_num_t)pin);
}

// Drive pin at level and latch it there, through deep sleep too (all of these are RTC GPIOs).
static void outputHeld(uint8_t pin, uint8_t level) {
	digitalWrite(pin, level);
	gpio_hold_en((gpio_num_t)pin);
}

static bool startReceive() {
	packetReady = false;
	digitalWrite(PIN_FEM_CTX, LOW);
	int16_t state = radio.startReceive();
	if (state != RADIOLIB_ERR_NONE) {
		Serial.printf("LoRa startReceive failed: %d\n", state);
		return false;
	}
	return true;
}

bool radioBegin() {
	outputUnheld(PIN_LORA_NSS, HIGH);
	outputUnheld(PIN_FEM_POWER, HIGH);
	delay(1);
	outputUnheld(PIN_FEM_CSD, HIGH);
	outputUnheld(PIN_FEM_CTX, LOW);

	SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
	// begin() also enables DIO2 as RF switch (drives the FEM CPS pin) and a 2-byte CRC.
	int16_t state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD,
	                            LORA_TX_POWER_DBM, LORA_PREAMBLE, LORA_TCXO_VOLTAGE);
	if (state != RADIOLIB_ERR_NONE) {
		Serial.printf("LoRa init failed: %d\n", state);
		return false;
	}
	radio.setPacketReceivedAction(onPacketReceived);
	return startReceive();
}

void radioSleep() {
	// Cold start: radioBegin() resets and configures the chip from scratch anyway.
	int16_t state = radio.sleep(false);
	if (state != RADIOLIB_ERR_NONE) {
		Serial.printf("LoRa sleep failed: %d\n", state);
	}
	outputHeld(PIN_FEM_CTX, LOW);
	outputHeld(PIN_FEM_CSD, LOW); // front end shutdown
	outputHeld(PIN_FEM_POWER, LOW);
	outputHeld(PIN_LORA_NSS, HIGH); // a falling NSS edge would wake the SX1262
}

bool radioSend(const uint8_t *data, size_t len) {
	digitalWrite(PIN_FEM_CTX, HIGH);
	int16_t state = radio.transmit(data, len);
	digitalWrite(PIN_FEM_CTX, LOW);
	if (state != RADIOLIB_ERR_NONE) {
		Serial.printf("LoRa transmit failed: %d\n", state);
	}
	// TX-done also raises DIO1; startReceive() clears that flag.
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
