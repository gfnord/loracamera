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

// Firmware version, shown at boot on both nodes. Bump it on every release; nodes must run
// the same protocol, so check both boards show the same version.
#define LORACAMERA_VERSION "0.2.0"

// ---- Transfer protocol shared by both nodes ----
// The remote node always starts a transfer: it wakes (timer with motion = 0, PIR with
// motion = 1), captures, and announces the capture. The base station only listens,
// requests the packets one by one, and says "done".
// MAX_TRANSFER_BUFFER (the largest file a node can send or receive) is set per board.
constexpr uint8_t LORA_TRANSFER_BUFFER = 250; // payload bytes per data packet (+2 header bytes)
static_assert(MAX_TRANSFER_BUFFER % LORA_TRANSFER_BUFFER == 0,
	"MAX_TRANSFER_BUFFER must be a whole number of packets");
// Packet types (first byte 0xF9): announcement F9 <type> nL nH, packet request F9 FE iL iH,
// done F9 FA. Data packets start with the packet index instead (iL iH + payload).
constexpr uint8_t PKT_MAGIC = 0xF9;
constexpr uint8_t PKT_ANNOUNCE_PICTURE = 0xC2;
constexpr uint8_t PKT_ANNOUNCE_AUDIO = 0xC3;
constexpr uint8_t PKT_REQUEST = 0xFE;
constexpr uint8_t PKT_DONE = 0xFA;
// Each announcement goes out ANNOUNCE_REPEAT times, ANNOUNCE_REPEAT_GAP_MS apart, so one
// copy survives a collision...
constexpr uint8_t ANNOUNCE_REPEAT = 5;
constexpr uint32_t ANNOUNCE_REPEAT_GAP_MS = 20;
// ...and is repeated every ANNOUNCE_GAP_MS until the first packet request arrives, at most
// ANNOUNCE_TRIES times; then the remote drops the capture and goes back to sleep.
constexpr uint32_t ANNOUNCE_GAP_MS = 5000;
constexpr uint8_t ANNOUNCE_TRIES = 6;
// The base station abandons a transfer when one packet hasn't arrived after this long...
constexpr uint32_t PACKET_GIVE_UP_MS = 20000;
// ...and the remote sleeps after this long without a request, or as soon as it gets "done".
constexpr uint32_t SERVE_IDLE_MS = 30000;
static_assert(SERVE_IDLE_MS > PACKET_GIVE_UP_MS, "the remote must outlast the base station's retries");
// The base station sends "done" this many times, ANNOUNCE_REPEAT_GAP_MS apart.
constexpr uint8_t DONE_REPEAT = 3;

// Audio capture, set by `audio` in the [loracamera] section of platformio.ini (remote only;
// the base station handles audio announcements whatever its own setting).
#ifndef LORACAMERA_AUDIO
#define LORACAMERA_AUDIO 0
#endif

// What wakes the remote, set by `motion` in the [loracamera] section of platformio.ini
// (remote only): 0 = a timer every `poll_interval` seconds, 1 = the PIR.
#ifndef LORACAMERA_MOTION
#define LORACAMERA_MOTION 0
#endif
static_assert(LORACAMERA_MOTION == 0 || LORACAMERA_MOTION == 1, "motion must be 0 or 1");
static_assert(!(LORACAMERA_MOTION && LORACAMERA_AUDIO), "motion = 1 needs audio = 0");

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
