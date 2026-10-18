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
// The remote sends its "N packets" RFT response this many times, this far apart,
// so one copy survives a collision.
constexpr uint8_t RFT_RESPONSE_REPEAT = 5;
constexpr uint32_t RFT_RESPONSE_GAP_MS = 20;

// Audio capture, set by `audio` in the [loracamera] section of platformio.ini.
#ifndef LORACAMERA_AUDIO
#define LORACAMERA_AUDIO 0
#endif

// PIR-triggered pictures, set by `motion` in the [loracamera] section of platformio.ini.
#ifndef LORACAMERA_MOTION
#define LORACAMERA_MOTION 0
#endif
static_assert(LORACAMERA_MOTION == 0 || LORACAMERA_MOTION == 1, "motion must be 0 or 1");
static_assert(!(LORACAMERA_MOTION && LORACAMERA_AUDIO), "motion = 1 needs audio = 0");

// ---- Motion mode (motion = 1): the remote wakes on the PIR and pushes the picture ----
// The remote announces a capture (F9 C2 n) every MOTION_ANNOUNCE_GAP_MS until the first
// packet request arrives, at most MOTION_ANNOUNCE_TRIES times, then goes back to sleep.
constexpr uint32_t MOTION_ANNOUNCE_GAP_MS = 5000;
constexpr uint8_t MOTION_ANNOUNCE_TRIES = 6;
// The local node abandons a transfer when one packet hasn't arrived after this long...
constexpr uint32_t MOTION_PACKET_GIVE_UP_MS = 20000;
// ...and the remote sleeps after this long without a request, or as soon as it gets F9 FA.
constexpr uint32_t MOTION_SERVE_IDLE_MS = 30000;
static_assert(MOTION_SERVE_IDLE_MS > MOTION_PACKET_GIVE_UP_MS,
	"the remote must outlast the local node's retries");
// The local node sends its "transfer done" packet (F9 FA) this many times, RFT_RESPONSE_GAP_MS apart.
constexpr uint8_t MOTION_DONE_REPEAT = 3;

// unsigned int packing functions
inline uint8_t u0(uint16_t in) { return in & 0xFF; }
inline uint8_t u1(uint16_t in) { return in >> 8; }
inline uint16_t u(uint8_t a, uint8_t b) { return (uint16_t)(b << 8) | a; }

// Switch Vext (OLED and Ve header supply) on.
void vextOn();

// Switch Vext off.
void vextOff();

// Pulse the OLED reset line; call with Vext on, before display init.
void oledReset();

// Power the front end, start the SX1262 and enter continuous receive.
// Returns false if the radio does not respond.
bool radioBegin();

// Put the SX1262 into sleep, power the front end down, and hold those pins through
// deep sleep. radioBegin() releases them again.
void radioSleep();

// Transmit one packet (blocking), then return to receive mode.
bool radioSend(const uint8_t *data, size_t len);

// Copy up to maxLen bytes of a received packet into buf. Returns the packet's
// full length (may exceed maxLen), or 0 if nothing (or only a packet with a
// bad CRC) arrived since the last call.
size_t radioReceive(uint8_t *buf, size_t maxLen);

// RSSI of the last packet returned by radioReceive().
int radioLastRssi();

// Airtime of a packet of len bytes with the current radio settings, in ms (rounded up).
uint32_t radioTimeOnAirMs(size_t len);
