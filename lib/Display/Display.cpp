#include "Display.h"

#if LORACAMERA_DISPLAY
#include <Board.h>
#include <SSD1306Wire.h>

static SSD1306Wire oled(0x3c, PIN_OLED_SDA, PIN_OLED_SCL, GEOMETRY_128_64);
static const char *station_title = "";

// ArialMT_Plain_10 is 13 px tall: the title on top, then four 13 px lines (12, 25, 38, 51).
static constexpr int16_t TITLE_HEIGHT = 12;
static constexpr int16_t LINE_HEIGHT = 13;

void displayBegin(const char *title) {
	station_title = title;
	oledReset();
	oled.init();
	oled.flipScreenVertically();
	oled.setFont(ArialMT_Plain_10);
}

void displayShow(const char *l1, const char *l2, const char *l3, const char *l4) {
	const char *lines[] = {l1, l2, l3, l4};
	oled.clear();
	oled.setTextAlignment(TEXT_ALIGN_CENTER);
	oled.drawString(64, 0, station_title);
	oled.setTextAlignment(TEXT_ALIGN_LEFT);
	for (uint8_t i = 0; i < 4; i++) {
		if (lines[i]) oled.drawString(0, TITLE_HEIGHT + i * LINE_HEIGHT, lines[i]);
	}
	oled.display();
}
#endif
