// Heltec WiFi LoRa 32 V4-R8 (HTIT-WBR8H, schematic V4.3.2): ESP32-S3R8, SX1262 + KCT8103L.
// Included by Board.h; don't include directly.
#pragma once

#include <Arduino.h>

// ---- Board pins (from the V4.3.2 schematic) ----
constexpr uint8_t PIN_VEXT_CTRL = 40; // gate of P-MOSFET Q2: LOW = Vext (OLED, Ve header pins) on
constexpr uint8_t PIN_LED = 46;
constexpr uint8_t PIN_OLED_SDA = 17;
constexpr uint8_t PIN_OLED_SCL = 18;
constexpr uint8_t PIN_OLED_RST = 21;

constexpr uint8_t PIN_LORA_NSS = 8;
constexpr uint8_t PIN_LORA_SCK = 9;
constexpr uint8_t PIN_LORA_MOSI = 10;
constexpr uint8_t PIN_LORA_MISO = 11;
constexpr uint8_t PIN_LORA_RST = 12;
constexpr uint8_t PIN_LORA_BUSY = 13;
constexpr uint8_t PIN_LORA_DIO1 = 14;

// KCT8103L front end. CPS is driven by SX1262 DIO2 (RF switch mode).
//   TX:      CSD=1 CTX=1 CPS=1
//   RX LNA:  CSD=1 CTX=0
constexpr uint8_t PIN_FEM_POWER = 7; // VFEM_Ctrl: enables the FEM LDO (HIGH = on)
constexpr uint8_t PIN_FEM_CSD = 2;   // chip enable (HIGH = on)
constexpr uint8_t PIN_FEM_CTX = 5;   // HIGH while transmitting, LOW for LNA receive

// ---- Radio chip settings ----
constexpr float LORA_TCXO_VOLTAGE = 1.8; // TCXO is powered from SX1262 DIO3
// SX1262 output power. The KCT8103L adds ~13 dB up to 13 dBm input, tapering to
// ~7 dB at 21 dBm: 7 -> ~20 dBm at the antenna, 21 -> ~28 dBm.
constexpr int8_t LORA_TX_POWER_DBM = 7;

// ---- Memory ----
// Largest file either node handles (500 packets). Static, in internal RAM.
constexpr uint32_t MAX_TRANSFER_BUFFER = 125000;

// ---- Project wiring: free GPIOs on the V4-R8 headers ----
// Remote node: VC0706 camera UART and its power switch.
#define CAMERATX_BOARDRX 47
#define CAMERARX_BOARDTX 48
#define CAMERA_ONOFF 41
// Remote node, motion = 1: PIR output, HIGH = motion. Must be an RTC GPIO (0-21) to wake
// the chip from deep sleep.
#define MOTION_PIR 6
static_assert(MOTION_PIR <= 21, "MOTION_PIR must be an RTC GPIO (0-21)");
// Remote node, audio = 1: I2S MEMS microphone.
#define I2S_BCK 4
#define I2S_LRC 3
#define I2S_DIN 6 // DOUT from I2S mic
// Local node, sdcard = 1: microSD breakout on its own SPI bus (HSPI). Header J3; GPIO38/39/41
// are also on the GNSS connector: leave that empty.
#define SD_CS 4    // J3 pin 15
#define SD_SCK 39  // J3 pin 10
#define SD_MISO 38 // J3 pin 11
#define SD_MOSI 41 // J3 pin 8
