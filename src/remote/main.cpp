#include <Arduino.h>
#include <Adafruit_VC0706.h>
#include <Board.h>
#include <Display.h>
#include "reencode.h"
#if LORACAMERA_AUDIO
#include "driver/i2s.h"
#endif
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

// What wakes the remote between captures (set in [loracamera]): motion = 1, the PIR;
// motion = 0, a timer every poll_interval seconds.
#if !LORACAMERA_MOTION
#ifndef LORACAMERA_POLL_INTERVAL
#define LORACAMERA_POLL_INTERVAL 600
#endif
static_assert(LORACAMERA_POLL_INTERVAL >= 1 && LORACAMERA_POLL_INTERVAL <= 86400,
	"poll_interval must be 1-86400 seconds");
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

#ifdef BOARD_HAS_PSRAM
// The camera's JPEG is read into PSRAM first, then re-encoded (or copied) into transfer_buffer.
#define CAMERA_JPEG_MAX (256 * 1024)
#else
// No PSRAM (Heltec V2): the camera's JPEG is read straight into transfer_buffer and sent
// unchanged; re-encoding needs a full-frame pixel buffer that only fits in PSRAM.
#define CAMERA_JPEG_MAX MAX_TRANSFER_BUFFER
#endif

// Camera, PIR and microphone pins are in the board header (lib/Board/<board>.h).

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

// Built-in test picture instead of the camera, set by `test_image` in [loracamera].
#ifndef LORACAMERA_TEST_IMAGE
#define LORACAMERA_TEST_IMAGE 0
#endif
static_assert(LORACAMERA_TEST_IMAGE == 0 || LORACAMERA_TEST_IMAGE == 1, "test_image must be 0 or 1");
#if LORACAMERA_TEST_IMAGE
#include "test_image.h"
static_assert(sizeof(TEST_JPEG) <= CAMERA_JPEG_MAX && sizeof(TEST_JPEG) <= MAX_TRANSFER_BUFFER,
	"the test JPEG must fit the camera and transfer buffers");
#endif

#if LORACAMERA_AUDIO
#define SAMPLE_RATE 11025
#define AUDIO_SECONDS 5
#define GAIN_FACTOR 2 
#define AUDIO_FRAME_SIZE 1024
#endif

Adafruit_VC0706 * cam;

uint8_t transfer_buffer[MAX_TRANSFER_BUFFER];
uint8_t lora_buffer[LORA_TRANSFER_BUFFER + 2]; // +2 for the uint16_t packet number header

uint32_t content_length = 0;
uint8_t *camera_jpeg = nullptr; // CAMERA_JPEG_MAX bytes: the JPEG as read from the camera


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
		if(audio_pointer + 2 > MAX_TRANSFER_BUFFER) return; // full: drop the rest
		int16_t sample = (int16_t) (small_buf[i] >> (12-GAIN_FACTOR));
		transfer_buffer[audio_pointer++] = (sample & 0xFF);
		transfer_buffer[audio_pointer++] = (sample >> 8);
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
	Serial.printf("loracamera remote station, firmware %s\n", LORACAMERA_VERSION);
	displayBegin("- Remote Station -");
	displayShow("Starting LoRa...", "Firmware " LORACAMERA_VERSION);
	if(!radioBegin()) {
		Serial.printf("Starting LoRa failed!\n");
		displayShow("Starting LoRa failed!");
		while(1) delay(1000);
	}

	// Set up the camera.  
	pinMode(CAMERA_ONOFF, OUTPUT);
	gpio_hold_dis((gpio_num_t)CAMERA_ONOFF); // held LOW through deep sleep by sleepUntilNextCapture()
	Serial1.begin(CAMERA_DEFAULT_BAUD,SERIAL_8N1, CAMERATX_BOARDRX, CAMERARX_BOARDTX);
	cam = new Adafruit_VC0706(&Serial1);
#ifdef BOARD_HAS_PSRAM
	camera_jpeg = (uint8_t *)ps_malloc(CAMERA_JPEG_MAX);
	if(!camera_jpeg) Serial.printf("No PSRAM for the camera buffer; pictures are disabled\n");
#else
	camera_jpeg = transfer_buffer;
#if LORACAMERA_JPEG_QUALITY > 0
	Serial.printf("No PSRAM on this board: jpeg_quality is ignored, camera JPEGs are sent unchanged\n");
#endif
#endif


	Serial.printf("I have %d free heap\n", ESP.getFreeHeap());
#if LORACAMERA_MOTION
	displayShow("Ready", "LoRa OK", "Sleeps until motion");
#else
	char every[32];
	snprintf(every, sizeof(every), "Wakes every %u s", (unsigned)LORACAMERA_POLL_INTERVAL);
	displayShow("Ready", "LoRa OK", every);
#endif
}

// Debug time taken to do things 
void print_time(const char * title, uint32_t length, long time) {
	Serial.printf("%s: %d bytes in %2.2fs. %2.2f bytes/s.\n", 
		title, length, (millis() - time) / 1000.0, (float)length / (float)(millis()-time) * 1000.0  );
}


#if LORACAMERA_AUDIO
void recordAudio() {
	Serial.printf("Recording %d seconds of audio\n", AUDIO_SECONDS);
	displayShow("Recording audio");
	long start_time = millis();
	audio_pointer = 0;
	while(millis() - start_time < (AUDIO_SECONDS*1000)) {
		read_audio();
		// 5 s fit the V4-R8's buffer; the V2's holds ~3.2 s.
		if(audio_pointer + 2 > MAX_TRANSFER_BUFFER) {
			Serial.printf("Audio buffer full (%u bytes), clip cut short\n", MAX_TRANSFER_BUFFER);
			break;
		}
	}
	content_length = audio_pointer;
	print_time("microphone", content_length, start_time);
}
#endif

// Move the camera's JPEG (camera_jpeg, jpglen bytes) into transfer_buffer and set
// content_length, re-encoding it first when jpeg_quality is set.
void prepareImage(uint32_t jpglen) {
#if LORACAMERA_JPEG_QUALITY > 0 && defined(BOARD_HAS_PSRAM)
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
	if(camera_jpeg != transfer_buffer) memcpy(transfer_buffer, camera_jpeg, jpglen);
	content_length = jpglen;
}

// Take a picture and load it into transfer_buffer
void takePicture() {
	content_length = 0;
#if LORACAMERA_TEST_IMAGE
	// No camera: the test JPEG goes through the same path as a camera frame.
	displayShow("Test image...");
	Serial.printf("test_image = 1: using the built-in %u-byte test JPEG, camera not used\n",
		(unsigned)sizeof(TEST_JPEG));
	if(camera_jpeg) {
		memcpy(camera_jpeg, TEST_JPEG, sizeof(TEST_JPEG));
		prepareImage(sizeof(TEST_JPEG));
	}
#else
	uint32_t jpglen = 0;
	displayShow("Taking picture...");
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
#endif
	char size_line[32];
	snprintf(size_line, sizeof(size_line), "%u bytes", content_length);
	displayShow(content_length ? "Picture ready" : "No picture",
		content_length ? size_line : "check camera / serial log");
}


// Number of data packets content_length bytes take.
uint16_t packetCount() {
	uint16_t packets = content_length / LORA_TRANSFER_BUFFER;
	if(content_length % LORA_TRANSFER_BUFFER != 0) packets++;
	return packets;
}

// Packet progress and the RSSI of the request on the status screen. Redrawn at most every
// 250 ms (and for the last packet): drawing takes I2C time the next request waits through.
void showSending(uint16_t packet) {
#if LORACAMERA_DISPLAY
	static uint32_t last_draw = 0;
	uint16_t packets = packetCount();
	if(packet + 1 < packets && millis() - last_draw < 250) return;
	last_draw = millis();
	char progress[24];
	char rssi_line[24];
	snprintf(progress, sizeof(progress), "%u / %u", packet + 1, packets);
	snprintf(rssi_line, sizeof(rssi_line), "rssi: %d", radioLastRssi());
	displayShow("Sending", progress, rssi_line);
#endif
}

// Tell the base station what's ready and how many data packets to request:
// F9 <type> nL nH, sent ANNOUNCE_REPEAT times.
void announce(uint8_t type, uint16_t packets) {
	char line[24];
	snprintf(line, sizeof(line), "%u packets", packets);
	displayShow("Announcing", type == PKT_ANNOUNCE_AUDIO ? "audio" : "picture", line);
	lora_buffer[0] = PKT_MAGIC;
	lora_buffer[1] = type;
	lora_buffer[2] = u0(packets);
	lora_buffer[3] = u1(packets);
	uint8_t send_repeat = ANNOUNCE_REPEAT;
	while(send_repeat-- > 0) {
		radioSend(lora_buffer, 4);
		delay(ANNOUNCE_REPEAT_GAP_MS);
	}
}

// Answer a packet request (F9 FE iL iH) for an existing packet. Returns false, sending
// nothing, for anything else.
bool servePacketRequest(const uint8_t *request, size_t packetSize) {
	if(packetSize != 4 || request[0] != PKT_MAGIC || request[1] != PKT_REQUEST) return false;
	uint16_t cd = u(request[2], request[3]); // cd is the # of the packet they want
	if(LORA_TRANSFER_BUFFER * (uint32_t)cd >= content_length) return false;
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
	showSending(cd);
	return true;
}

#if LORACAMERA_AUDIO
// With audio = 1 (timer wake-ups only) captures alternate picture, audio, picture, ...
// RTC memory survives deep sleep; it starts at false (a picture) after power-on.
static RTC_DATA_ATTR bool next_is_audio = false;
#endif

// Capture (picture, or audio clip every other time with audio = 1) and push it: announce
// it until the base station starts requesting packets, then serve them until it sends
// "done" or goes quiet.
void pushCapture() {
	uint8_t type = PKT_ANNOUNCE_PICTURE;
#if LORACAMERA_AUDIO
	if(next_is_audio) {
		recordAudio();
		type = PKT_ANNOUNCE_AUDIO;
	} else
#endif
	{
		takePicture();
	}
	uint16_t packets = packetCount();
	if(packets == 0) {
		Serial.printf("Nothing to send\n");
		return;
	}
	const char *what = type == PKT_ANNOUNCE_AUDIO ? "audio" : "picture";
	uint8_t request[4];
	uint8_t announcements = 0;
	bool requested = false;
	uint32_t last = 0; // last announcement, then last request once requests arrive
	while(true) {
		size_t packetSize = radioReceive(request, sizeof(request));
		if(servePacketRequest(request, packetSize)) {
			requested = true;
			last = millis();
		} else if(packetSize == 2 && request[0] == PKT_MAGIC && request[1] == PKT_DONE) {
			Serial.printf("Transfer done\n");
			displayShow("Transfer done");
			break;
		} else if(requested) {
			if(millis() - last > SERVE_IDLE_MS) {
				Serial.printf("No request for %u s, stopping\n", SERVE_IDLE_MS / 1000);
				break;
			}
		} else if(announcements == 0 || millis() - last > ANNOUNCE_GAP_MS) {
			if(announcements == ANNOUNCE_TRIES) {
				Serial.printf("The base station never answered; %s dropped\n", what);
				displayShow("No answer from", "the base station");
				return;
			}
			Serial.printf("Announcing %s: %u bytes in %d packets\n", what, content_length, packets);
			announce(type, packets);
			announcements++;
			last = millis();
		}
		delay(20);
	}
#if LORACAMERA_AUDIO
	next_is_audio = !next_is_audio; // only after the base station took it
#endif
}

// Power everything down and deep-sleep until the next capture: the PIR output going HIGH
// (motion = 1) or the poll_interval timer (motion = 0). Never returns: the chip restarts
// at setup() when it wakes.
void sleepUntilNextCapture() {
	radioSleep();
	Serial1.end(); // don't drive the camera's RX line while it's unpowered
	vextOff();
	// The camera switch pin would float in deep sleep, and the boost converter's EN may be
	// pulled up: hold it LOW. Digital pads also need the deep-sleep hold, which latches all
	// of them (Vext stays off, the radio stays deselected).
	digitalWrite(CAMERA_ONOFF, LOW);
	gpio_hold_en((gpio_num_t)CAMERA_ONOFF);
	gpio_deep_sleep_hold_en();
#if LORACAMERA_MOTION
	// The pull-down keeps an unplugged PIR from waking the board; RTC pulls need the
	// RTC peripherals powered during sleep.
	esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
	rtc_gpio_pullup_dis((gpio_num_t)MOTION_PIR);
	rtc_gpio_pulldown_en((gpio_num_t)MOTION_PIR);
	esp_sleep_enable_ext1_wakeup(1ULL << MOTION_PIR, ESP_EXT1_WAKEUP_ANY_HIGH);
	Serial.printf("Sleeping until the PIR on GPIO%d sees motion\n", MOTION_PIR);
#else
	esp_sleep_enable_timer_wakeup((uint64_t)LORACAMERA_POLL_INTERVAL * 1000000ULL);
	Serial.printf("Sleeping %u s until the next picture\n", (unsigned)LORACAMERA_POLL_INTERVAL);
#endif
	Serial.flush();
	esp_deep_sleep_start();
}

// Runs once per wake-up.
void loop() {
#if LORACAMERA_MOTION
	// After power-on or reset just arm the PIR: pictures are taken on motion only.
	if(esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
		Serial.printf("Motion detected\n");
		displayShow("Motion detected");
		pushCapture();
	}
#else
	// Timer wake-up, or power-on: a capture right away confirms the link after installing.
	pushCapture();
#endif
	sleepUntilNextCapture();
}
