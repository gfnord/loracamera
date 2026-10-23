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

// Polls (20 ms each) waiting for one data packet before it's requested again (≈ 4 s).
#define RESPONSE_TIMER 200
// How long setup() waits for Wi-Fi before starting without it (it keeps reconnecting).
#define WIFI_CONNECT_TIMEOUT_MS 20000

#define AUDIO 1
#define PICTURE 0
// Type of the capture being received, from the remote's announcement.
uint8_t content_type = PICTURE;

// TODO -- bank this so that we can have more room
uint8_t transfer_buffer[MAX_TRANSFER_BUFFER];
uint8_t lora_buffer[LORA_TRANSFER_BUFFER + 2]; // +2 for the uint16_t packet number header



void setup() { 
	Serial.begin(115200);
	vextOn();
	Serial.printf("loracamera base station, firmware %s\n", LORACAMERA_VERSION);
	displayBegin("- Base Station -");
	displayShow("Starting LoRa...", "Firmware " LORACAMERA_VERSION);

	if(!radioBegin()) {
		Serial.printf("Starting LoRa failed!\n");
		displayShow("Starting LoRa failed!");
		while(1) delay(1000);
	}

	displayShow("LoRa OK", "Connecting to Wi-Fi...");
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

	displayShow("Ready", wifi_line, sd_line, "Listening for remote");
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

	if(content_type == AUDIO) {
		sprintf(put_url, "%s/audio.raw", SERVER_URL);
		http.begin(put_url);
		http.addHeader("Content-Type", "audio/L8");
	} else {
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

// Request one data packet: F9 FE iL iH.
void request_packet(uint16_t packet_number) {
	lora_buffer[0] = PKT_MAGIC;
	lora_buffer[1] = PKT_REQUEST;
	lora_buffer[2] = u0(packet_number);
	lora_buffer[3] = u1(packet_number);
	radioSend(lora_buffer, 4);
	delay(20);
}

void update_status(uint16_t packet_number, uint16_t packets, int rssi) {
	char progress[64];
	char rssi_line[32];
	sprintf(progress, "%d / %d [%2.2f%%]", 
		packet_number, packets, ((float)packet_number/packets)*100.0f);
	sprintf(rssi_line, "rssi: %d", rssi);
	Serial.printf("%s %s\n", progress, rssi_line);
	displayShow("Receiving", progress, rssi_line);
}

// Pull an announced capture from the remote, packet by packet, then tell it "done" and
// save and upload what arrived.
void handle_packets(uint16_t packets) {
	uint32_t read_pointer = 0;
	long time = millis();
	const char *skipped = nullptr; // why nothing was requested, for the status screen
	// A remote on another board may have a bigger buffer than this one.
	if((uint32_t)packets * LORA_TRANSFER_BUFFER > MAX_TRANSFER_BUFFER) {
		Serial.printf("%d packets don't fit this board's %u-byte buffer; skipped\n",
			packets, MAX_TRANSFER_BUFFER);
		skipped = "Too big for this board";
		packets = 0; // request nothing; the remote still gets "done"
	}
	// The remote repeats its announcement; wait until it's done before the first request,
	// or that request is sent while the remote is still transmitting and gets lost.
	delay(ANNOUNCE_REPEAT * (radioTimeOnAirMs(4) + ANNOUNCE_REPEAT_GAP_MS));
	for(uint16_t i=0;i<packets;i++) {
		bool ok = false;
		uint32_t packet_start = millis();
		while(!ok && millis() - packet_start <= PACKET_GIVE_UP_MS) {
			request_packet(i);
			uint8_t response_timer = RESPONSE_TIMER;
			while(response_timer-- > 0 && !ok) {
				size_t packetSize = radioReceive(lora_buffer, sizeof(lora_buffer));
				if(packetSize) {
					uint8_t a = lora_buffer[0];
					uint8_t b = lora_buffer[1];
					uint16_t packet_number = u(a, b);
					if(a == PKT_MAGIC && packetSize < 5) {
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
							ok = true;
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
			// The remote is probably back asleep: the rest won't come either.
			Serial.printf("Remote stopped answering; abandoning the transfer at packet %d\n", i);
			break;
		}
	}
	// "Done": the remote can go back to sleep before the SD write and upload.
	for(uint8_t n=0;n<DONE_REPEAT;n++) {
		lora_buffer[0] = PKT_MAGIC;
		lora_buffer[1] = PKT_DONE;
		radioSend(lora_buffer, 2);
		delay(ANNOUNCE_REPEAT_GAP_MS);
	}
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
	// The remote starts every transfer (on its timer or its PIR): wait for its announcement.
	size_t packetSize = radioReceive(lora_buffer, sizeof(lora_buffer));
	if(packetSize == 4 && lora_buffer[0] == PKT_MAGIC
			&& (lora_buffer[1] == PKT_ANNOUNCE_PICTURE || lora_buffer[1] == PKT_ANNOUNCE_AUDIO)) {
		content_type = lora_buffer[1] == PKT_ANNOUNCE_AUDIO ? AUDIO : PICTURE;
		uint16_t packets = u(lora_buffer[2], lora_buffer[3]);
		Serial.printf("%s announced, %d packets\n", content_type == AUDIO ? "Audio" : "Picture", packets);
		if(packets) handle_packets(packets);
	}
	delay(10);
}
