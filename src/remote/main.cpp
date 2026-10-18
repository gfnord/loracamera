#include <Arduino.h>
#include <Adafruit_VC0706.h>
#include <HeltecV4R8.h>
#include "reencode.h"
#if LORACAMERA_AUDIO
#include "driver/i2s.h"
#endif

// Re-encoding of the camera JPEG, set in the [loracamera] section of platformio.ini.
#ifndef LORACAMERA_JPEG_QUALITY
#define LORACAMERA_JPEG_QUALITY 0
#endif
#ifndef LORACAMERA_NIGHT_GRAYSCALE
#define LORACAMERA_NIGHT_GRAYSCALE 0
#endif
#ifndef LORACAMERA_NIGHT_MAX_CHROMA
#define LORACAMERA_NIGHT_MAX_CHROMA 12
#endif
static_assert(LORACAMERA_JPEG_QUALITY >= 0 && LORACAMERA_JPEG_QUALITY <= 100,
	"jpeg_quality must be 0-100");
static_assert(LORACAMERA_NIGHT_GRAYSCALE == 0 || LORACAMERA_NIGHT_GRAYSCALE == 1,
	"night_grayscale must be 0 or 1");
static_assert(LORACAMERA_NIGHT_MAX_CHROMA >= 0 && LORACAMERA_NIGHT_MAX_CHROMA <= 255,
	"night_max_chroma must be 0-255");

// The camera's JPEG is read into PSRAM first, then re-encoded (or copied) into transfer_buffer.
#define CAMERA_JPEG_MAX (256 * 1024)

// Pins (free GPIOs on the V4-R8 headers)
#define CAMERATX_BOARDRX 47
#define CAMERARX_BOARDTX 48
#define CAMERA_ONOFF 41

// The VC0706 always starts at this speed: the baud change below is not saved on the camera.
#define CAMERA_DEFAULT_BAUD 38400
#ifndef LORACAMERA_CAMERA_BAUD
#define LORACAMERA_CAMERA_BAUD CAMERA_DEFAULT_BAUD
#endif
#ifndef LORACAMERA_CAMERA_COMPRESSION
#define LORACAMERA_CAMERA_COMPRESSION 0x36
#endif
static_assert(LORACAMERA_CAMERA_BAUD == 38400 || LORACAMERA_CAMERA_BAUD == 115200,
	"camera_baud must be 38400 or 115200");
static_assert(LORACAMERA_CAMERA_COMPRESSION >= 0 && LORACAMERA_CAMERA_COMPRESSION <= 255,
	"camera_compression must be 0-255");

#if LORACAMERA_AUDIO
#define SAMPLE_RATE 11025
#define AUDIO_SECONDS 5
#define GAIN_FACTOR 2 
#define AUDIO_FRAME_SIZE 1024

#define I2S_BCK 4
#define I2S_LRC 3
#define I2S_DIN 6 // DOUT from I2S mic
#endif

Adafruit_VC0706 * cam;

uint8_t transfer_buffer[MAX_TRANSFER_BUFFER];
uint8_t lora_buffer[LORA_TRANSFER_BUFFER + 2]; // +2 for the uint16_t packet number header

uint32_t content_length = 0;
uint8_t *camera_jpeg = nullptr; // CAMERA_JPEG_MAX bytes in PSRAM: the JPEG as read from the camera


#if LORACAMERA_AUDIO
uint32_t audio_pointer = 0;

//i2s configuration
i2s_config_t i2s_config = {
     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
     .sample_rate = SAMPLE_RATE,
     .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
     .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, 
     .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
     .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // high interrupt priority
     .dma_buf_count = 4,
     .dma_buf_len = AUDIO_FRAME_SIZE,
     .use_apll = false,
     .tx_desc_auto_clear = false,
     .fixed_mclk = 0,   //Interrupt level 1
    };
    
i2s_pin_config_t pin_config = {
    .mck_io_num = I2S_PIN_NO_CHANGE, // no MCLK; leaving this 0 would drive GPIO0
    .bck_io_num = I2S_BCK,
    .ws_io_num = I2S_LRC, 
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DIN,
};

void setup_i2s(void) {
	pinMode(I2S_DIN, INPUT);
	pinMode(I2S_BCK, OUTPUT);
	pinMode(I2S_LRC, OUTPUT);
	i2s_driver_install((i2s_port_t)I2S_NUM_0, &i2s_config, 0, NULL);
	i2s_set_pin((i2s_port_t)I2S_NUM_0, &pin_config);
	i2s_set_sample_rates((i2s_port_t)I2S_NUM_0, SAMPLE_RATE);
	i2s_start(I2S_NUM_0);
}

void read_audio(void) {
	size_t bytes_read = 0;
	int32_t small_buf[AUDIO_FRAME_SIZE/4];
	i2s_read((i2s_port_t)I2S_NUM_0, small_buf, AUDIO_FRAME_SIZE, &bytes_read, portMAX_DELAY );
	for(uint32_t i=0;i<(bytes_read/4);i++) { 
		int16_t sample = (int16_t) (small_buf[i] >> (12-GAIN_FACTOR));
		transfer_buffer[audio_pointer++] = (sample & 0xFF);
		transfer_buffer[audio_pointer++] = (sample >> 8);
		if(audio_pointer-1 == MAX_TRANSFER_BUFFER) audio_pointer = 0;
	}
}
#endif

// Reset the camera. It normally comes up at CAMERA_DEFAULT_BAUD, but if its power
// wasn't actually cut since the last picture it is still at LORACAMERA_CAMERA_BAUD.
bool resetCamera() {
	Serial1.updateBaudRate(CAMERA_DEFAULT_BAUD);
	if(cam->reset()) return true;
	if(LORACAMERA_CAMERA_BAUD == CAMERA_DEFAULT_BAUD) return false;
	Serial1.updateBaudRate(LORACAMERA_CAMERA_BAUD);
	bool ok = cam->reset();
	Serial1.updateBaudRate(CAMERA_DEFAULT_BAUD); // a system reset reloads the default speed
	return ok;
}

// Switch the camera serial link to LORACAMERA_CAMERA_BAUD for the picture readout.
// Not persistent: the camera reverts to CAMERA_DEFAULT_BAUD on power-up or reset.
void speedUpCamera() {
	if(LORACAMERA_CAMERA_BAUD == CAMERA_DEFAULT_BAUD) return;
	cam->setBaud115200();
	delay(100);
	Serial1.updateBaudRate(LORACAMERA_CAMERA_BAUD);
	if(cam->getVersion()) {
		Serial.printf("Camera serial at %d baud\n", LORACAMERA_CAMERA_BAUD);
	} else {
		Serial.printf("Camera didn't answer at %d baud, staying at %d\n",
			LORACAMERA_CAMERA_BAUD, CAMERA_DEFAULT_BAUD);
		Serial1.updateBaudRate(CAMERA_DEFAULT_BAUD);
	}
}

void turnOnCamera() {
	digitalWrite(CAMERA_ONOFF, HIGH); 
	delay(1000);
	int t = resetCamera();
	Serial.printf("Turning on camera; reset with %d\n", t);
	delay(100);
	cam->getVersion();
	delay(100);
	cam->setImageSize(VC0706_640x480); 
	delay(100);
	// Chip register: lost on reset, so set it after resetCamera().
	cam->setCompression(LORACAMERA_CAMERA_COMPRESSION);
	delay(100);
	speedUpCamera();
}

void turnOffCamera() {
	Serial.printf("Turning off camera\n");
	digitalWrite(CAMERA_ONOFF, LOW); 
	delay(1000);
}



void setup() {
	Serial.begin(115200);
#if LORACAMERA_AUDIO
	setup_i2s();
#endif
	vextOn();
	if(!radioBegin()) {
		Serial.printf("Starting LoRa failed!\n");
		while(1) delay(1000);
	}

	// Set up the camera.  
	pinMode(CAMERA_ONOFF, OUTPUT);
	Serial1.begin(CAMERA_DEFAULT_BAUD,SERIAL_8N1, CAMERATX_BOARDRX, CAMERARX_BOARDTX);
	cam = new Adafruit_VC0706(&Serial1);
	camera_jpeg = (uint8_t *)ps_malloc(CAMERA_JPEG_MAX);
	if(!camera_jpeg) Serial.printf("No PSRAM for the camera buffer; pictures are disabled\n");


	Serial.printf("I have %d free heap\n", ESP.getFreeHeap());
}

// Debug time taken to do things 
void print_time(const char * title, uint32_t length, long time) {
	Serial.printf("%s: %d bytes in %2.2fs. %2.2f bytes/s.\n", 
		title, length, (millis() - time) / 1000.0, (float)length / (float)(millis()-time) * 1000.0  );
}


#if LORACAMERA_AUDIO
void recordAudio() {
	Serial.printf("Recording %d seconds of audio\n", AUDIO_SECONDS);
	long start_time = millis();
	audio_pointer = 0;
	while(millis() - start_time < (AUDIO_SECONDS*1000)) {
		read_audio();
	}
	content_length = audio_pointer;
	print_time("microphone", content_length, start_time);
}
#endif

// Move the camera's JPEG (camera_jpeg, jpglen bytes) into transfer_buffer and set
// content_length, re-encoding it first when jpeg_quality is set.
void prepareImage(uint32_t jpglen) {
#if LORACAMERA_JPEG_QUALITY > 0
	long start = millis();
	ReencodeResult r;
	bool ok = reencodeJpeg(camera_jpeg, jpglen, transfer_buffer, MAX_TRANSFER_BUFFER,
		LORACAMERA_JPEG_QUALITY, LORACAMERA_NIGHT_GRAYSCALE, LORACAMERA_NIGHT_MAX_CHROMA, &r);
	if(ok) {
		Serial.printf("Re-encoded %ux%u %s (chroma %u, quality %d): %u -> %u bytes in %.2fs\n",
			r.width, r.height, r.grayscale ? "grayscale" : "colour", r.chroma,
			LORACAMERA_JPEG_QUALITY, jpglen, r.len, (millis() - start) / 1000.0);
		if(r.len < jpglen) {
			content_length = r.len;
			return;
		}
	}
	Serial.printf("Sending the camera JPEG unchanged\n");
#endif
	if(jpglen > MAX_TRANSFER_BUFFER) {
		Serial.printf("Camera JPEG (%u bytes) doesn't fit the %u-byte transfer buffer; nothing to send\n",
			jpglen, MAX_TRANSFER_BUFFER);
		content_length = 0;
		return;
	}
	memcpy(transfer_buffer, camera_jpeg, jpglen);
	content_length = jpglen;
}

// Take a picture and load it into transfer_buffer
void takePicture() {
	uint32_t jpglen = 0;
	content_length = 0;
	turnOnCamera();
	Serial.printf("Taking pic\n");
	// Restart the camera
	cam->resumeVideo();
	delay(1000);
	if(cam->takePicture()) {
		delay(100);
		jpglen = cam->frameLength();
		delay(100);
		Serial.printf("len of frame is %d\n", jpglen);
		if(!camera_jpeg || jpglen == 0 || jpglen > CAMERA_JPEG_MAX) {
			Serial.printf("Can't read a %u-byte frame into the %u-byte camera buffer\n",
				jpglen, camera_jpeg ? CAMERA_JPEG_MAX : 0);
			jpglen = 0;
		} else {
			long pic_time = millis();
			// The camera has a tiny (100 byte) buffer for transfers, so we read a bit at a time into ESP ram
			uint32_t idx = 0;
			uint32_t remaining = jpglen;
			while(remaining > 0) {
				uint8_t bytesToRead = min((uint32_t)64, remaining);
				uint8_t * camera_buffer = cam->readPicture(bytesToRead);
				if(camera_buffer) {
					memcpy(camera_jpeg + idx, camera_buffer, bytesToRead);
					idx += bytesToRead;
					remaining -= bytesToRead;      
				} else {
					Serial.printf("problem reading data from camera -- read %d bytes\n", idx);
					delay(100);
				}
			}
			print_time("camera", jpglen, pic_time);
		}
	} else {
		Serial.printf("Couldn't take pic\n");
	}
	turnOffCamera();
	// Re-encode with the camera already off.
	if(jpglen) prepareImage(jpglen);
}


void loop() { 
	// Check for inbound packets and respond to them
	uint8_t request[4];
	size_t packetSize = radioReceive(request, sizeof(request));
	if(packetSize == 2) { // probably a request for transmission
		uint8_t a = request[0];
		uint8_t b = request[1];
		delay(20);
		if(a==0xF9 && (b==0xFC || (LORACAMERA_AUDIO && b==0xFD))) { 
			// Both load into transfer_buffer, both set content_length
			// both these take multiple seconds to complete, the local is waiting here
			if(b==0xFC) { // take picture
				takePicture();
			}
#if LORACAMERA_AUDIO
			else { // record audio
				recordAudio();
			}
#endif
			// Now respond to the RFT with the # of packets to expect
			uint16_t packets = content_length / LORA_TRANSFER_BUFFER;
			if(content_length % LORA_TRANSFER_BUFFER != 0) packets++;
			Serial.printf("RFT received. Content length is %d; buffer count %d. Packets to send is %d\n", 
				content_length, LORA_TRANSFER_BUFFER, packets);
			lora_buffer[0] = 0xF9;
			lora_buffer[1] = 0xC2;
			lora_buffer[2] = u0(packets);
			lora_buffer[3] = u1(packets);
			uint8_t send_repeat = RFT_RESPONSE_REPEAT;
			while(send_repeat-- > 0) {
				radioSend(lora_buffer, 4);
				delay(RFT_RESPONSE_GAP_MS);
			}
		}
	} else if (packetSize == 4) {  // probably a request for packet
		uint8_t a = request[0];
		uint8_t b = request[1];
		uint16_t cd = u(request[2], request[3]); // cd is the # of the packet they want
		if(a==0xF9 && b==0xFE && LORA_TRANSFER_BUFFER * (uint32_t)cd < content_length) { // request for an existing packet
			uint32_t byte_start = LORA_TRANSFER_BUFFER * cd;
			uint8_t bytes_to_send = LORA_TRANSFER_BUFFER;

			if(cd == (content_length / LORA_TRANSFER_BUFFER)) { // if it's last packet
				bytes_to_send = content_length % LORA_TRANSFER_BUFFER;
			}
			Serial.printf("Packet %d was requested. byte_start is %d. content_length %d\n", 
				cd, byte_start, content_length);

			lora_buffer[0] = u0(cd);
			lora_buffer[1] = u1(cd);
			// +2 is for the uint16_t header that has the packet # in it
			for(uint32_t i=byte_start;i<byte_start+bytes_to_send;i++) {
				lora_buffer[i-byte_start+2] = transfer_buffer[i];
			}
			delay(5); // turnaround guard: the requester needs ~1 ms to get back into receive mode
			radioSend(lora_buffer, bytes_to_send+2);
			delay(20);
		} 
	}
	delay(20);

}
