#include "Display.h"

#if LORACAMERA_DISPLAY
#include <Board.h>
#include <SSD1306Wire.h>

static SSD1306Wire oled(0x3c, PIN_OLED_SDA, PIN_OLED_SCL, GEOMETRY_128_64);

void displayBegin() {
	oledReset();
	oled.init();
	oled.flipScreenVertically();
	oled.setFont(ArialMT_Plain_10);
}

void displayShow(const char *l1, const char *l2, const char *l3, const char *l4) {
	const char *lines[] = {l1, l2, l3, l4};
	oled.clear();
	for (uint8_t i = 0; i < 4; i++) {
		if (lines[i]) oled.drawString(0, i * 16, lines[i]);
	}
	oled.display();
}
#endif
