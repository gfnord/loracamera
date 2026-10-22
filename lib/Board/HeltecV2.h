// Heltec WiFi LoRa 32 V2: ESP32-D0WDQ6 (no PSRAM), 8 MB flash, SX1276 on PA_BOOST,
// CP2102 USB-UART. Included by Board.h; don't include directly.
#pragma once

#include <Arduino.h>

// ---- Board pins (Heltec's V2 pin map, as in the Arduino core's heltec_wifi_lora_32_V2 variant) ----
constexpr uint8_t PIN_VEXT_CTRL = 21; // LOW = Vext (OLED, Vext header pin) on
constexpr uint8_t PIN_LED = 25;
constexpr uint8_t PIN_OLED_SDA = 4;
constexpr uint8_t PIN_OLED_SCL = 15;
constexpr uint8_t PIN_OLED_RST = 16;

constexpr uint8_t PIN_LORA_NSS = 18;
constexpr uint8_t PIN_LORA_SCK = 5;
constexpr uint8_t PIN_LORA_MOSI = 27;
constexpr uint8_t PIN_LORA_MISO = 19;
constexpr uint8_t PIN_LORA_RST = 14;
constexpr uint8_t PIN_LORA_DIO0 = 26; // RX done / TX done
constexpr uint8_t PIN_LORA_DIO1 = 35;

// ---- Radio chip settings ----
// SX1276 output power on PA_BOOST, 2-17 dBm (20 dBm is only rated for a 1 % duty cycle,
// far below what a picture transfer needs). There is no external amplifier.
constexpr int8_t LORA_TX_POWER_DBM = 17;
// Over-current protection limit. RadioLib's default (60 mA) is below the ~90 mA the PA draws
// at 17 dBm and would clip it; 100 mA is the chip's reset value.
constexpr uint8_t LORA_CURRENT_LIMIT_MA = 100;

// ---- Memory ----
// Largest file either node handles (288 packets). The ESP32's static RAM can't hold the
// V4-R8's 125000 bytes next to Wi-Fi: this is what the local node still links with.
constexpr uint32_t MAX_TRANSFER_BUFFER = 72000;

// ---- Project wiring: free GPIOs on the V2 headers ----
// Remote node: VC0706 camera UART and its power switch. Not GPIO2 for the switch: it's a
// strapping pin, and a boost converter EN pulled up on it would block flashing.
#define CAMERATX_BOARDRX 23
#define CAMERARX_BOARDTX 17
#define CAMERA_ONOFF 22
// Remote node, motion = 1: PIR output, HIGH = motion. Must be an RTC GPIO with an internal
// pull-down to wake the chip from deep sleep (0, 2, 4, 12-15, 25-27, 32, 33).
#define MOTION_PIR 32
// Remote node, audio = 1: I2S MEMS microphone (the original V2 build's pins).
// GPIO12 is a strapping pin: nothing may pull it HIGH at reset.
#define I2S_BCK 13
#define I2S_LRC 12
#define I2S_DIN 39 // DOUT from I2S mic
// Local node, sdcard = 1: microSD breakout on its own SPI bus (HSPI).
#define SD_CS 23
#define SD_SCK 22
#define SD_MISO 33
#define SD_MOSI 17
