#include "Board.h"

// Both boards switch Vext through a P-MOSFET: LOW = on.
void vextOn() {
	pinMode(PIN_VEXT_CTRL, OUTPUT);
	digitalWrite(PIN_VEXT_CTRL, LOW);
	delay(50); // let the Vext supply settle
}

void vextOff() {
	pinMode(PIN_VEXT_CTRL, OUTPUT);
	digitalWrite(PIN_VEXT_CTRL, HIGH);
}

void oledReset() {
	pinMode(PIN_OLED_RST, OUTPUT);
	digitalWrite(PIN_OLED_RST, LOW);
	delay(20);
	digitalWrite(PIN_OLED_RST, HIGH);
	delay(20);
}
