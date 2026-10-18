#include "HeltecV4R8.h"

#include <RadioLib.h>
#include <SPI.h>

static SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY, SPI);

static volatile bool packetReady = false;
static int lastRssi = 0;

static void IRAM_ATTR onPacketReceived() { packetReady = true; }

void vextOn() {
	pinMode(PIN_VEXT_CTRL, OUTPUT);
	digitalWrite(PIN_VEXT_CTRL, LOW);
	delay(50); // let the Vext LDO settle
}

void oledReset() {
	pinMode(PIN_OLED_RST, OUTPUT);
	digitalWrite(PIN_OLED_RST, LOW);
	delay(20);
	digitalWrite(PIN_OLED_RST, HIGH);
	delay(20);
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
	pinMode(PIN_FEM_POWER, OUTPUT);
	digitalWrite(PIN_FEM_POWER, HIGH);
	delay(1);
	pinMode(PIN_FEM_CSD, OUTPUT);
	digitalWrite(PIN_FEM_CSD, HIGH);
	pinMode(PIN_FEM_CTX, OUTPUT);
	digitalWrite(PIN_FEM_CTX, LOW);

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
