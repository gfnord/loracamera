#include <Arduino.h>
#include <Board.h>
#include <Display.h>
#ifndef LORACAMERA_SDCARD
#define LORACAMERA_SDCARD 0
#endif
static_assert(LORACAMERA_SDCARD == 0 || LORACAMERA_SDCARD == 1, "sdcard must be 0 or 1");
#if LORACAMERA_SDCARD
#include "storage.h"
#endif
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// Wi-Fi credentials, upload server and its bearer token: all in auth.h (not committed).
#include "auth.h"
#ifndef SERVER_URL
#error "Define SERVER_URL in src/local/auth.h, e.g. http://loracam.example.com (no trailing slash)"
#endif
#ifndef UPLOAD_TOKEN
#error "Define UPLOAD_TOKEN in src/local/auth.h: the server's UPLOAD_TOKEN"
#endif
const char * ssid = WIFI_SSID ;
const char * password = WIFI_PASS ;

#define REQUEST_TIMER 200
#define RESPONSE_TIMER 200
// How long setup() waits for Wi-Fi before starting without it (it keeps reconnecting).
#define WIFI_CONNECT_TIMEOUT_MS 20000
#if !LORACAMERA_MOTION
// Seconds between requests, set by `poll_interval` in the [loracamera] section of platformio.ini.
#ifndef LORACAMERA_POLL_INTERVAL
#define LORACAMERA_POLL_INTERVAL 10
#endif
static_assert(LORACAMERA_POLL_INTERVAL >= 0 && LORACAMERA_POLL_INTERVAL <= 86400,
	"poll_interval must be 0-86400 seconds");
#define CHECK_EVERY_MS (LORACAMERA_POLL_INTERVAL * 1000UL)
// The max the remote device will take to acquire image / audio
#define MAX_RFT_RESPONSE_MS 30000
#endif

unsigned int counter = 0;

#define AUDIO 1
#define PICTURE 0

#if !LORACAMERA_MOTION
long lastCheckTime = 0;
#endif
uint8_t content_type = PICTURE;

// TODO -- bank this so that we can have more room
uint8_t transfer_buffer[MAX_TRANSFER_BUFFER];
uint8_t lora_buffer[LORA_TRANSFER_BUFFER + 2]; // +2 for the uint16_t packet number header



void setup() { 
	Serial.begin(115200);
	vextOn();
	displayBegin();
	displayShow("Base station", "Starting LoRa...");

	if(!radioBegin()) {
		Serial.printf("Starting LoRa failed!\n");
		displayShow("Base station", "Starting LoRa failed!");
		while(1) delay(1000);
	}

	displayShow("Base station", "LoRa OK", "Connecting to Wi-Fi...");
#if LORACAMERA_SDCARD
	const char *sd_line = storageBegin() ? "SD card OK" : "SD card not found";
#else
	const char *sd_line = nullptr;
#endif

	// Wi-Fi is needed only for uploads: don't wait forever, so pictures still reach
	// the SD card without it. The driver keeps reconnecting in the background.
	WiFi.setAutoReconnect(true);
	WiFi.begin(ssid, password);
	long wifi_start = millis();
	while(WiFi.status() != WL_CONNECTED && millis() - wifi_start < WIFI_CONNECT_TIMEOUT_MS) {
		delay(500);
	}
	char wifi_line[40];
	if(WiFi.status() == WL_CONNECTED) {
		Serial.printf("Wi-Fi connected, IP %s\n", WiFi.localIP().toString().c_str());
		snprintf(wifi_line, sizeof(wifi_line), "Wi-Fi %s", WiFi.localIP().toString().c_str());
	} else {
		wl_status_t st = WiFi.status();
		const char *why = st == WL_NO_SSID_AVAIL ? "SSID not found (2.4 GHz only)"
			: st == WL_CONNECT_FAILED ? "connect failed (password?)"
			: "no connection";
		Serial.printf("Wi-Fi not connected yet to \"%s\": %s (status %d); continuing, uploads wait for it\n",
			ssid, why, st);
		snprintf(wifi_line, sizeof(wifi_line), "Wi-Fi: %s", st == WL_NO_SSID_AVAIL ? "SSID not found" : "not connected");
	}
	// UTC from NTP once Wi-Fi is up; sets the SD card's file timestamps.
	configTime(0, 0, "pool.ntp.org");


	Serial.printf("I have %d free heap\n", ESP.getFreeHeap());

#if LORACAMERA_MOTION
	displayShow("Base station ready", wifi_line, sd_line, "Waiting for motion");
#else
	displayShow("Base station ready", wifi_line, sd_line, "Polling the remote");
#endif
}

// Debug time taken to do things 
void print_time(const char * title, uint32_t length, long time) {
	Serial.printf("%s: %d bytes in %2.2fs. %2.2f bytes/s.\n", 
		title, length, (millis() - time) / 1000.0, (float)length / (float)(millis()-time) * 1000.0  );
}


// Upload buf to SERVER_URL. Returns a short result for the status screen.
const char *put_to_server(uint8_t * buf, uint32_t length) {
	if(WiFi.status() != WL_CONNECTED) {
		Serial.printf("Wi-Fi not connected, upload skipped\n");
		return "Upload skipped: no Wi-Fi";
	}
	char put_url[255];
	HTTPClient http;
	long time = millis();

#if LORACAMERA_AUDIO
	if(content_type == AUDIO) {
		sprintf(put_url, "%s/audio.raw", SERVER_URL);
		http.begin(put_url);
		http.addHeader("Content-Type", "audio/L8");
	} else
#endif
	{
		sprintf(put_url, "%s/image.jpg", SERVER_URL);
		http.begin(put_url);
		http.addHeader("Content-Type", "image/jpg");		
	}
	http.addHeader("Authorization", "Bearer " UPLOAD_TOKEN);
	http.addHeader("Content-Length", String(length));
	int code = http.PUT(buf,length); 
	http.end();
	print_time("server", length, time);
	if(code < 200 || code >= 300) {
		Serial.printf("Upload failed: %s\n", code < 0 ? http.errorToString(code).c_str() : String(code).c_str());
		return "Upload failed";
	}
	return "Uploaded";
}

// request a particular packet
void request_packet(uint16_t packet_number) {
	lora_buffer[0] = 0xF9;
	lora_buffer[1] = 0xFE;
	lora_buffer[2] = u0(packet_number);
	lora_buffer[3] = u1(packet_number);
	radioSend(lora_buffer, 4);
	delay(20);
}

// say, how many packets you got for me
// type == 0 for image, 1 for audio
#if !LORACAMERA_MOTION
uint16_t request_transmission() {
	uint8_t request_timer = REQUEST_TIMER;
	while(request_timer-- > 0) {
		lora_buffer[0] = 0xF9;
		lora_buffer[1] = 0xFC + content_type;
		Serial.printf("Sending request for transmission of type %d...\n", content_type);
		char attempt[24];
		snprintf(attempt, sizeof(attempt), "attempt %d", REQUEST_TIMER - request_timer);
		displayShow("Requesting", content_type == AUDIO ? "audio" : "picture", attempt);
		radioSend(lora_buffer, 2);

		// Wait for either audio or picture to capture
		long wfr_time = millis();
		while(millis() - wfr_time < MAX_RFT_RESPONSE_MS) {
			size_t packetSize = radioReceive(lora_buffer, sizeof(lora_buffer));
			if(packetSize==4) {
				uint16_t cd = u(lora_buffer[2], lora_buffer[3]);
				if(lora_buffer[0] == 0xF9 && lora_buffer[1] == 0xC2) {
					Serial.printf("Receiving rft response, %d packets\n", cd);
					return cd;
				}
			}
			delay(20);
		}
		Serial.printf("Receive timer timed out\n");
	}
	Serial.printf("Request timer timed out\n");
	return 0;
}
#endif

void update_status(uint16_t packet_number, uint16_t packets, int rssi) {
	char progress[64];
	char rssi_line[32];
	sprintf(progress, "%d / %d [%2.2f%%]", 
		packet_number, packets, ((float)packet_number/packets)*100.0f);
	sprintf(rssi_line, "rssi: %d", rssi);
	Serial.printf("%s %s\n", progress, rssi_line);
	displayShow("Receiving", progress, rssi_line);
}

void handle_packets(uint16_t packets) {
	uint32_t read_pointer = 0;
	long time = millis();
	Serial.printf("Got back %d packets from the rft\n", packets);
	const char *skipped = nullptr; // why nothing was requested, for the status screen
	// A remote on another board may have a bigger buffer than this one.
	if((uint32_t)packets * LORA_TRANSFER_BUFFER > MAX_TRANSFER_BUFFER) {
		Serial.printf("%d packets don't fit this board's %u-byte buffer; skipped\n",
			packets, MAX_TRANSFER_BUFFER);
		skipped = "Too big for this board";
		packets = 0; // request nothing; in motion mode the remote still gets "done"
	}
	// The remote repeats its RFT response; wait until it's done before the first request,
	// or that request is sent while the remote is still transmitting and gets lost.
	delay(RFT_RESPONSE_REPEAT * (radioTimeOnAirMs(4) + RFT_RESPONSE_GAP_MS));
	for(uint16_t i=0;i<packets;i++) {
		uint8_t request_timer = REQUEST_TIMER;
		uint8_t ok = 0;
#if LORACAMERA_MOTION
		uint32_t packet_start = millis();
#endif
		while(request_timer-- > 0 && !ok) {
#if LORACAMERA_MOTION
			if(millis() - packet_start > MOTION_PACKET_GIVE_UP_MS) break;
#endif
			request_packet(i);
			uint8_t response_timer = RESPONSE_TIMER;
			while(response_timer-- > 0 && !ok) {
				size_t packetSize = radioReceive(lora_buffer, sizeof(lora_buffer));
				if(packetSize) {
					uint8_t a = lora_buffer[0];
					uint8_t b = lora_buffer[1];
					uint16_t packet_number = u(a, b);
					if(a == 0xF9 && packetSize < 5) {
						Serial.printf("got magic packet, discarding\n");
						delay(20);
					} else if(packetSize < 2 || packetSize > sizeof(lora_buffer)
							|| read_pointer + (packetSize - 2) > MAX_TRANSFER_BUFFER) {
						Serial.printf("Malformed packet (%d bytes), discarding\n", packetSize);
					} else {
						if(packet_number == i) {
							for(uint32_t j=0;j<packetSize-2;j++) { 
								transfer_buffer[read_pointer++] = lora_buffer[j+2];    
							}
							ok = 1;
							update_status(packet_number, packets, radioLastRssi());
						} else {
							Serial.printf("Mismatched packet\n");
						}
					}
				}
				if(!ok) delay(20); // poll interval while waiting for the packet
			}
			if(!ok) {
				Serial.printf("response timer timeout\n");
				delay(20);
			}
		}
		if(!ok) {
			Serial.printf("request timer timeout\n");
#if LORACAMERA_MOTION
			// The remote is probably back asleep: the rest won't come either.
			Serial.printf("Remote stopped answering; abandoning the transfer at packet %d\n", i);
			break;
#endif
		}
	}
#if LORACAMERA_MOTION
	// "Done" (F9 FA): the remote can go back to sleep before the SD write and upload.
	for(uint8_t n=0;n<MOTION_DONE_REPEAT;n++) {
		lora_buffer[0] = 0xF9;
		lora_buffer[1] = 0xFA;
		radioSend(lora_buffer, 2);
		delay(RFT_RESPONSE_GAP_MS);
	}
#endif
	Serial.printf("Loaded all packets, total %d\n", read_pointer);
	print_time("lora", read_pointer, time);
	char received[40];
	snprintf(received, sizeof(received), "%u bytes in %lu s", read_pointer, (millis() - time) / 1000);
	if(read_pointer == 0) {
		displayShow("Nothing received", received, skipped);
		return;
	}
	displayShow("Received", received, "Saving...");
#if LORACAMERA_SDCARD
	const char *sd_line = storageSave(transfer_buffer, read_pointer, content_type == AUDIO ? "raw" : "jpg")
		? "Saved to SD card" : "SD card save failed";
#else
	const char *sd_line = nullptr;
#endif
	displayShow("Received", received, sd_line, put_to_server(transfer_buffer, read_pointer));
}


void loop() {
#if LORACAMERA_MOTION
	// The remote pushes a picture when its PIR triggers: wait for its announcement.
	size_t packetSize = radioReceive(lora_buffer, sizeof(lora_buffer));
	if(packetSize == 4 && lora_buffer[0] == 0xF9 && lora_buffer[1] == 0xC2) {
		uint16_t packets = u(lora_buffer[2], lora_buffer[3]);
		Serial.printf("Motion picture announced, %d packets\n", packets);
		if(packets) handle_packets(packets);
	}
#else
	uint16_t packets = 0;
	if(millis() - lastCheckTime > CHECK_EVERY_MS) {
		lastCheckTime = millis();
		packets = request_transmission();
	}
	if(packets) {
		Serial.printf("if packets! %d\n", packets);
		handle_packets(packets);
		lastCheckTime = millis();
#if LORACAMERA_AUDIO
		if(content_type == AUDIO) { content_type = PICTURE; } else { content_type = AUDIO; } // flip type
#endif
	}
#endif
	delay(10);
}
