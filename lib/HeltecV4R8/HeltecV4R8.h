// Board support for the Heltec WiFi LoRa 32 V4-R8 (HTIT-WBR8H, schematic V4.3.2)
// plus the LoRa link settings shared by the local and remote nodes.
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

// ---- LoRa link settings: both nodes must agree ----
constexpr float LORA_FREQ_MHZ = 915.0;   // Canada / US 902-928 MHz
constexpr float LORA_BW_KHZ = 250.0;
constexpr uint8_t LORA_SF = 7;
constexpr uint8_t LORA_CR = 5;           // 4/5
constexpr uint8_t LORA_SYNC_WORD = 0x12; // private network
constexpr uint16_t LORA_PREAMBLE = 8;
constexpr float LORA_TCXO_VOLTAGE = 1.8; // TCXO is powered from SX1262 DIO3
// SX1262 output power. The KCT8103L adds ~13 dB up to 13 dBm input, tapering to
// ~7 dB at 21 dBm: 7 -> ~20 dBm at the antenna (same as the V2 build), 21 -> ~28 dBm.
constexpr int8_t LORA_TX_POWER_DBM = 7;

// ---- Transfer protocol sizes shared by both nodes ----
constexpr uint32_t MAX_TRANSFER_BUFFER = 125000;
constexpr uint8_t LORA_TRANSFER_BUFFER = 250; // payload bytes per data packet (+2 header bytes)

// unsigned int packing functions
inline uint8_t u0(uint16_t in) { return in & 0xFF; }
inline uint8_t u1(uint16_t in) { return in >> 8; }
inline uint16_t u(uint8_t a, uint8_t b) { return (uint16_t)(b << 8) | a; }

// Switch Vext (OLED and Ve header supply) on.
void vextOn();

// Pulse the OLED reset line; call with Vext on, before display init.
void oledReset();

// Power the front end, start the SX1262 and enter continuous receive.
// Returns false if the radio does not respond.
bool radioBegin();

// Transmit one packet (blocking), then return to receive mode.
bool radioSend(const uint8_t *data, size_t len);

// Copy up to maxLen bytes of a received packet into buf. Returns the packet's
// full length (may exceed maxLen), or 0 if nothing (or only a packet with a
// bad CRC) arrived since the last call.
size_t radioReceive(uint8_t *buf, size_t maxLen);

// RSSI of the last packet returned by radioReceive().
int radioLastRssi();
