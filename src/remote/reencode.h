// Re-encode the camera's JPEG on the ESP32-S3 to make it smaller before sending.
#pragma once

#include <Arduino.h>

struct ReencodeResult {
	size_t len;      // bytes written to dst
	uint16_t width;
	uint16_t height;
	uint8_t chroma;  // mean colourfulness, 0 (gray) - 255
	bool grayscale;  // encoded as a grayscale JPEG
};

// Decode the JPEG in src and encode it again into dst at `quality` (1-100).
// If nightGrayscale is set and the image's mean chroma is <= nightMaxChroma
// (an IR night shot is almost colourless), it is encoded as grayscale.
// Returns false if decoding or encoding fails or the result exceeds dstCap;
// dst then holds no usable image.
bool reencodeJpeg(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstCap,
                  uint8_t quality, bool nightGrayscale, uint8_t nightMaxChroma,
                  ReencodeResult *result);
