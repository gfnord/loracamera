// Board support, selected by the PlatformIO environment, plus the LoRa link settings
// and transfer protocol shared by the local and remote nodes.
#pragma once

#include <Arduino.h>

// Pins, radio chip settings and the project's wiring for the selected board.
#if defined(LORACAMERA_BOARD_HELTEC_V4R8)
#include "HeltecV4R8.h"
#elif defined(LORACAMERA_BOARD_HELTEC_V2)
#include "HeltecV2.h"
#else
#error "No board selected: build one of the environments in platformio.ini"
#endif

// ---- LoRa link settings: both nodes must agree, whichever board they run on ----
constexpr float LORA_FREQ_MHZ = 915.0;   // Canada / US 902-928 MHz
constexpr float LORA_BW_KHZ = 250.0;
constexpr uint8_t LORA_SF = 7;
constexpr uint8_t LORA_CR = 5;           // 4/5
constexpr uint8_t LORA_SYNC_WORD = 0x12; // private network (same on air for SX1262 and SX1276)
constexpr uint16_t LORA_PREAMBLE = 8;

// ---- Transfer protocol sizes shared by both nodes ----
// MAX_TRANSFER_BUFFER (the largest file a node can send or receive) is set per board.
constexpr uint8_t LORA_TRANSFER_BUFFER = 250; // payload bytes per data packet (+2 header bytes)
static_assert(MAX_TRANSFER_BUFFER % LORA_TRANSFER_BUFFER == 0,
	"MAX_TRANSFER_BUFFER must be a whole number of packets");
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

// Switch Vext (OLED and Vext header supply) on.
void vextOn();

// Switch Vext off.
void vextOff();

// Pulse the OLED reset line; call with Vext on, before display init.
void oledReset();

// Start the radio (and its front end, if the board has one) and enter continuous receive.
// Returns false if the radio does not respond.
bool radioBegin();

// Put the radio to sleep and hold its control pins through deep sleep.
// radioBegin() releases them again.
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
