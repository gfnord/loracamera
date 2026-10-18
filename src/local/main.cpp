#include <Arduino.h>
#include <HeltecV4R8.h>
#include <SSD1306Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>

// Wifi details & server for uploads
#define SERVER_URL "http://192.168.0.150:8000"
#include "auth.h"
const char * ssid = WIFI_SSID ;
const char * password = WIFI_PASS ;

#define REQUEST_TIMER 200
#define RESPONSE_TIMER 200
#define CHECK_EVERY_MS 10000
// The max the remote device will take to acquire image / audio
#define MAX_RFT_RESPONSE_MS 30000
#define AUDIO_SECONDS 5

unsigned int counter = 0;

#define AUDIO 1
#define PICTURE 0

long lastCheckTime = 0;
uint8_t content_type = PICTURE;

SSD1306Wire display(0x3c, PIN_OLED_SDA, PIN_OLED_SCL, GEOMETRY_128_64);

// TODO -- bank this so that we can have more room
uint8_t transfer_buffer[MAX_TRANSFER_BUFFER];
uint8_t lora_buffer[LORA_TRANSFER_BUFFER + 2]; // +2 for the uint16_t packet number header



void setup() { 
	Serial.begin(115200);
	vextOn();
	oledReset();
	display.init();
	display.flipScreenVertically();  
	display.setFont(ArialMT_Plain_10);

	if(!radioBegin()) {
		Serial.printf("Starting LoRa failed!\n");
		display.clear();
		display.drawString(0, 0, "Starting LoRa failed!");
		display.display();
		while(1) delay(1000);
	}

	// Set up WIFI 
	WiFi.begin(ssid, password);
	while(WiFi.status() != WL_CONNECTED) {
		delay(500);
	}


	Serial.printf("I have %d free heap\n", ESP.getFreeHeap());

	display.clear();
	display.display();
}

// Debug time taken to do things 
void print_time(const char * title, uint32_t length, long time) {
	Serial.printf("%s: %d bytes in %2.2fs. %2.2f bytes/s.\n", 
		title, length, (millis() - time) / 1000.0, (float)length / (float)(millis()-time) * 1000.0  );
}


void put_to_server(uint8_t * buf, uint32_t length) {
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
	http.addHeader("Content-Length", String(length));
	http.PUT(buf,length); 
	http.end();
	print_time("server", length, time);
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
uint16_t request_transmission() {
	uint8_t request_timer = REQUEST_TIMER;
	while(request_timer-- > 0) {
		lora_buffer[0] = 0xF9;
		lora_buffer[1] = 0xFC + content_type;
		Serial.printf("Sending request for transmission of type %d...\n", content_type);
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

void update_status(uint16_t packet_number, uint16_t packets, int rssi) {
	char message[255];
	sprintf(message, "%d / %d [%2.2f%%]", 
		packet_number, packets, ((float)packet_number/packets)*100.0f);

	display.clear();
	display.setFont(ArialMT_Plain_10);
	display.drawString(0, 0, message);
	Serial.print(message);
	sprintf(message, " rssi: %d\n", rssi);
	display.drawString(0, 15, message);
	display.display();
	Serial.print(message);

}

void handle_packets(uint16_t packets) {
	uint32_t read_pointer = 0;
	long time = millis();
	Serial.printf("Got back %d packets from the rft\n", packets);
	for(uint16_t i=0;i<packets;i++) {
		uint8_t request_timer = REQUEST_TIMER;
		uint8_t ok = 0;
		while(request_timer-- > 0 && !ok) {
			delay(100); // delay 100ms between packet requests
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
					} else if(packetSize < 2 || packetSize > sizeof(lora_buffer)) {
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
				delay(20);
			}
			if(!ok) Serial.printf("response timer timeout\n");
			delay(20);
		}
		if(!ok) Serial.printf("request timer timeout\n");
	}
	Serial.printf("Loaded all packets, total %d\n", read_pointer);
	print_time("lora", read_pointer, time);
	put_to_server(transfer_buffer, read_pointer);
}


void loop() {
	
	uint16_t packets = 0;
	if(millis() - lastCheckTime > CHECK_EVERY_MS) {
		lastCheckTime = millis();
		packets = request_transmission();
	}
	if(packets) {
		Serial.printf("if packets! %d\n", packets);
		handle_packets(packets);
		lastCheckTime = millis();
		if(content_type == AUDIO) { content_type = PICTURE; } else { content_type = AUDIO; } // flip type
	}
	delay(10);
	


}
