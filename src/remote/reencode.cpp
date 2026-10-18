#include "reencode.h"

#include <esp_heap_caps.h>
#include <img_converters.h>

namespace {

struct DecodeCtx {
	const uint8_t *src;
	uint8_t *bgr; // full image, 3 bytes per pixel in PSRAM
	uint16_t width;
	uint16_t height;
	uint64_t chromaSum;
};

size_t jpgRead(void *arg, size_t index, uint8_t *buf, size_t len) {
	DecodeCtx *ctx = static_cast<DecodeCtx *>(arg);
	if (buf) memcpy(buf, ctx->src + index, len); // buf is NULL when the decoder skips data
	return len;
}

// Called once with data == NULL and the full image size, then once per decoded
// block (RGB888), then once more with data == NULL at the end.
bool jpgWrite(void *arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data) {
	DecodeCtx *ctx = static_cast<DecodeCtx *>(arg);
	if (!data) {
		if (x == 0 && y == 0) {
			ctx->width = w;
			ctx->height = h;
			ctx->bgr = static_cast<uint8_t *>(
				heap_caps_malloc((size_t)w * h * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
		}
		return true;
	}
	// The decoder ignores a failed start callback; refusing blocks aborts the decode.
	if (!ctx->bgr) return false;

	for (uint16_t row = 0; row < h; row++) {
		uint8_t *out = ctx->bgr + ((size_t)(y + row) * ctx->width + x) * 3;
		for (uint16_t col = 0; col < w; col++, data += 3, out += 3) {
			uint8_t r = data[0], g = data[1], b = data[2];
			// fmt2jpg's PIXFORMAT_RGB888 input is stored B, G, R.
			out[0] = b;
			out[1] = g;
			out[2] = r;
			ctx->chromaSum += max(r, max(g, b)) - min(r, min(g, b));
		}
	}
	return true;
}

struct OutCtx {
	uint8_t *dst;
	size_t cap;
	size_t len;
	bool overflow;
};

// The encoder ignores short writes, so overflow is tracked here.
size_t jpgOut(void *arg, size_t index, const void *data, size_t len) {
	OutCtx *ctx = static_cast<OutCtx *>(arg);
	if (ctx->overflow || index + len > ctx->cap) {
		ctx->overflow = true;
		return len;
	}
	memcpy(ctx->dst + index, data, len);
	ctx->len = index + len;
	return len;
}

} // namespace

bool reencodeJpeg(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstCap,
                  uint8_t quality, bool nightGrayscale, uint8_t nightMaxChroma,
                  ReencodeResult *result) {
	DecodeCtx dec = {src, nullptr, 0, 0, 0};
	esp_err_t err = esp_jpg_decode(srcLen, JPG_SCALE_NONE, jpgRead, jpgWrite, &dec);
	size_t pixels = (size_t)dec.width * dec.height;
	if (err != ESP_OK || !dec.bgr || pixels == 0) {
		Serial.printf("JPEG decode failed (err %d, %ux%u%s)\n", err, dec.width, dec.height,
			(pixels && !dec.bgr) ? ", out of PSRAM" : "");
		heap_caps_free(dec.bgr);
		return false;
	}
	result->width = dec.width;
	result->height = dec.height;
	result->chroma = (uint8_t)(dec.chromaSum / pixels);
	result->grayscale = nightGrayscale && result->chroma <= nightMaxChroma;

	uint8_t *pixelData = dec.bgr;
	size_t pixelLen = pixels * 3;
	pixformat_t format = PIXFORMAT_RGB888;
	if (result->grayscale) {
		// In place: pixel i's gray byte lands at or before its BGR bytes.
		for (size_t i = 0; i < pixels; i++) {
			const uint8_t *p = dec.bgr + i * 3;
			dec.bgr[i] = (uint8_t)((29 * p[0] + 150 * p[1] + 77 * p[2]) >> 8); // BT.601 luma
		}
		pixelLen = pixels;
		format = PIXFORMAT_GRAYSCALE;
	}

	OutCtx out = {dst, dstCap, 0, false};
	bool ok = fmt2jpg_cb(pixelData, pixelLen, dec.width, dec.height, format, quality, jpgOut, &out);
	heap_caps_free(dec.bgr);
	if (!ok || out.overflow) {
		Serial.printf("JPEG encode failed%s\n", out.overflow ? ": output larger than buffer" : "");
		return false;
	}
	result->len = out.len;
	return true;
}
