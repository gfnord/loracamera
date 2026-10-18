# Configuration reference

All settings are compile-time. Change them, rebuild, and reflash. **`audio` in the `[loracamera]` section of `platformio.ini` and the settings in `lib/HeltecV4R8/HeltecV4R8.h` are shared: reflash both boards after changing any of them.** `display` only affects the local node, and the camera settings only the remote node.

## Audio capture (optional)

Audio is **off by default**, so the link carries only pictures. To build both nodes with microphone support, set this in `platformio.ini`:

```ini
[loracamera]
audio = 1
```

Then rebuild and reflash **both** boards. The value reaches the code as the `LORACAMERA_AUDIO` macro (`-DLORACAMERA_AUDIO=${loracamera.audio}`).

| | `audio = 0` (default) | `audio = 1` |
|---|---|---|
| Local node requests | pictures only | pictures and audio, alternating |
| Remote node | camera only; I2S driver and recording code are not compiled in; `F9 FD` requests are ignored | also records `AUDIO_SECONDS` of audio on `F9 FD` |
| Microphone wiring | not needed | GPIO3/4/6, see [hardware.md](hardware.md#remote-node-wiring) |
| Uploads | `/image.jpg` | `/image.jpg` and `/audio.raw` |
| Cycle time (SF7) | a 48 KB camera JPEG ≈ 46 s, a re-encoded 10–20 KB one ≈ 10–20 s | adds ≈ 1.8 min of airtime for every 5 s clip (≈ 110 KB) |

Both nodes must be built with the same value. A local node built with audio sends `F9 FD` requests that a picture-only remote node never answers. The local node then waits out its retries (up to ~100 min) instead of fetching pictures.

## OLED display (optional)

The V4-R8 is sold with or without a 0.96" OLED. By default the firmware assumes **no display**. To show transfer progress on the OLED, set this in `platformio.ini`:

```ini
[loracamera]
display = 1
```

Then rebuild and reflash the **local** node. The remote node never uses the display, so the setting doesn't affect it. The value reaches the code as `LORACAMERA_DISPLAY`.

| | `display = 0` (default) | `display = 1` |
|---|---|---|
| Progress (`i / n [%]`, RSSI) | serial log only | serial log and OLED |
| `Starting LoRa failed!` | serial log only | serial log and OLED |
| OLED driver (ThingPulse SSD1306) | not compiled in | linked; resets the OLED on GPIO21 and drives it over I2C on GPIO17/18 |
| GPIO17, 18, 21 on the local node | free | used by the OLED |

Vext (GPIO40) is switched on either way, because it also powers the `Ve` header pins.

## Radio link (`lib/HeltecV4R8/HeltecV4R8.h`)

| Constant | Default | Meaning |
|---|---|---|
| `LORA_FREQ_MHZ` | `915.0` | Carrier frequency. The HTIT-WBR8H covers 863–928 MHz |
| `LORA_BW_KHZ` | `250.0` | LoRa bandwidth |
| `LORA_SF` | `7` | Spreading factor (7–12). Higher means more range and slower transfers |
| `LORA_CR` | `5` | Coding rate denominator, i.e. 4/5 |
| `LORA_SYNC_WORD` | `0x12` | Private-network sync word. Change it on both nodes to ignore other LoRa traffic |
| `LORA_PREAMBLE` | `8` | Preamble length in symbols |
| `LORA_TCXO_VOLTAGE` | `1.8` | Board-specific, don't change |
| `LORA_TX_POWER_DBM` | `7` | SX1262 output power (−9 to 22), see below |

RadioLib also enables a 2-byte payload CRC. Packets that fail it are dropped and requested again.

### Choosing the frequency

In Canada and the US use the 902–928 MHz ISM band. 915 MHz sits in the middle of it. Pick any value inside the band, set the same value on both nodes, and stay clear of the band edges with 250 kHz bandwidth.

This firmware transmits on a single fixed channel with no frequency hopping. Make sure that is acceptable for your use under ISED RSS-247 (Canada) or FCC Part 15.247 (US).

### TX power

`LORA_TX_POWER_DBM` sets the SX1262 output. The KCT8103L amplifier adds gain on top of it, and the gain shrinks as the input rises. Measured gain, from Meshtastic's KCT8103L table:

| `LORA_TX_POWER_DBM` | Front-end gain | ≈ Antenna power |
|---|---|---|
| 0 | +13 dB | 13 dBm |
| 7 (default) | +13 dB | 20 dBm (same as the original V2 build) |
| 13 | +13 dB | 26 dBm |
| 17 | +11 dB | 28 dBm |
| 21 | +7 dB | 28 dBm |

Above ~17 the output saturates near Heltec's 28 ± 1 dBm rating, while current draw keeps rising (~750 mA at 27 dBm). Raising SF usually buys more range per milliamp than raising power. Antenna gain adds to EIRP, which is the figure regulators limit.

### Range vs. speed

Airtime for one full data packet (252 bytes, explicit header, CRC on, CR 4/5). Transfer times also count the request packet and the protocol's polling and turnaround time (see [protocol.md](protocol.md#throughput)), assuming no retries:

| SF / BW | Data packet airtime | ≈ 48 KB JPEG (192 packets) | ≈ 20 KB re-encoded JPEG (80 packets) | ≈ 5 s audio (442 packets) |
|---|---|---|---|---|
| SF7 / 250 kHz (default) | 0.20 s | 46 s | 19 s | 1.8 min |
| SF9 / 250 kHz | 0.63 s | 2.3 min | 57 s | 5.2 min |
| SF10 / 125 kHz | 2.3 s | 8.0 min | 3.3 min | 18 min |

These are calculated with the Semtech airtime formula, not measured.

**Limit:** the local node waits `RESPONSE_TIMER` × 20 ms ≈ 4 s for each data packet. The remote node's poll interval and 5 ms turnaround guard plus the data packet airtime must fit in that window. That rules out SF11 and SF12 at 125 kHz (≈ 4.9 s and 9 s per packet) unless you lengthen the window by raising the `delay(20)` inside the response loop of `handle_packets()`. `RESPONSE_TIMER` is a `uint8_t` and can't go above 255. RadioLib enables low-data-rate optimisation automatically where the SX1262 needs it (SF11/SF12 at 125 kHz).

## Transfer protocol (`lib/HeltecV4R8/HeltecV4R8.h`)

| Constant | Default | Meaning |
|---|---|---|
| `MAX_TRANSFER_BUFFER` | `125000` | Size of the capture/receive buffer on each node (bytes). Captures must fit |
| `LORA_TRANSFER_BUFFER` | `250` | Payload bytes per data packet. Each packet also carries a 2-byte header, and the LoRa maximum is 255 |
| `RFT_RESPONSE_REPEAT` / `RFT_RESPONSE_GAP_MS` | `5` / `20` | The remote sends its "N packets" reply this many times, this far apart. The local node waits out the repeats (from the radio's actual airtime) before its first packet request |

## Local node (`src/local/main.cpp`)

| Constant | Default | Meaning |
|---|---|---|
| `SERVER_URL` | `"http://192.168.0.150:8000"` | Upload target, without a trailing slash. Pictures are PUT to `/image.jpg`, and audio (if enabled) to `/audio.raw` |
| `WIFI_SSID`, `WIFI_PASS` | from `auth.h` | 2.4 GHz network |
| `CHECK_EVERY_MS` | `10000` | Pause after a finished transfer before the next request |
| `MAX_RFT_RESPONSE_MS` | `30000` | How long to wait for the remote node to capture and answer a request. Must exceed the remote's capture time: picture ≈ 2.5 s of fixed delays + the image read (≈ 20 s at 640×480 and 38400 baud, per the original code comment; less at 115200) + 1 s power-off + re-encoding (≈ 1 s [estimate]); audio (if enabled) = `AUDIO_SECONDS` |
| `REQUEST_TIMER` | `200` | Retries for a request, and per data packet. Must fit in a `uint8_t` (≤ 255) |
| `RESPONSE_TIMER` | `200` | Polls (20 ms each) waiting for each data packet, ≈ 4 s. Must be ≤ 255 |

With the default `audio = 0`, every request is for a **picture**. With `audio = 1`, the first request after boot is for a picture, and the type then alternates between picture and audio after each completed transfer.

## Remote node (`src/remote/main.cpp`)

### Pins

| Define | Default | Function |
|---|---|---|
| `CAMERATX_BOARDRX` | 47 | UART1 RX (camera TX) |
| `CAMERARX_BOARDTX` | 48 | UART1 TX (camera RX) |
| `CAMERA_ONOFF` | 41 | Camera power control, HIGH = on |
| `I2S_BCK` | 4 | Mic bit clock (`audio = 1` only) |
| `I2S_LRC` | 3 | Mic word select (`audio = 1` only) |
| `I2S_DIN` | 6 | Mic data (`audio = 1` only) |

Pick replacements only from the free pins listed in [hardware.md](hardware.md#v4-r8-pin-map): 3, 4, 6, 15, 16, 38, 39, 41, 42, 47, 48. Avoid 45 and 46, which are strapping pins, and 43/44, where the ROM bootloader prints at boot.

### Audio

These apply only when built with `audio = 1` (see [Audio capture](#audio-capture-optional)).

| Constant | Default | Meaning |
|---|---|---|
| `SAMPLE_RATE` | `11025` | Hz |
| `AUDIO_SECONDS` | `5` | Recording length. `SAMPLE_RATE × AUDIO_SECONDS × 2` must stay below `MAX_TRANSFER_BUFFER` |
| `GAIN_FACTOR` | `2` | Each 32-bit I2S sample is shifted right by `12 − GAIN_FACTOR` before truncating to 16 bits. +1 doubles the volume |
| `AUDIO_FRAME_SIZE` | `1024` | I2S DMA buffer and read size, in bytes |

The output is mono signed 16-bit little-endian PCM (s16le). Default: 5 s × 11025 Hz × 2 B ≈ 110 KB.

### Camera

These camera settings live in the `[loracamera]` section of `platformio.ini`:

| Setting | Default | Meaning |
|---|---|---|
| `camera_baud` | `115200` | Serial speed for the picture readout: `38400` or `115200` (the VC0706 maximum). Other values fail the build |
| `camera_compression` | `54` | VC0706 JPEG compression register, `0`–`255`. `54` (0x36) is the camera's own default; higher means smaller, blockier JPEGs. Out-of-range values fail the build |

How they're applied, on every picture, in `turnOnCamera()`:

1. The camera powers up at 38400 baud. The baud change isn't saved on the camera; its saved speed lives in its EEPROM, which the firmware never writes. `resetCamera()` resets it at 38400. If that gets no answer (the camera's power wasn't actually cut since the last picture), it retries at `camera_baud`.
2. `setCompression(camera_compression)` writes the compression register. It's a volatile chip register, so it's set after the reset.
3. `speedUpCamera()` switches the camera and `Serial1` to `camera_baud`, then checks the link with a version request. On success the remote logs `Camera serial at 115200 baud`. Otherwise it logs `Camera didn't answer at 115200 baud, staying at 38400` and reads the picture at 38400.

Picking `camera_compression`:
- **With re-encoding on (`jpeg_quality` > 0, the default):** the camera's JPEG only crosses the serial cable and is then re-compressed, so keep it at `54` for a clean source. Raising it saves serial readout time, but compressing twice adds artifacts.
- **With re-encoding off (`jpeg_quality = 0`):** this is the only size control. Raise it until the image gets too blocky (watch `len of frame is …` in the remote log), then back off.

Fixed camera settings in `src/remote/main.cpp`:

| Setting | Default | Where |
|---|---|---|
| Resolution | `VC0706_640x480` | `turnOnCamera()`; alternatives `VC0706_320x240`, `VC0706_160x120` |
| Chunk size | 64 bytes | `takePicture()`; the camera buffer is 100 bytes |
| Camera buffer | `CAMERA_JPEG_MAX` = 256 KB | PSRAM buffer the camera's JPEG is read into. Larger frames are skipped |

Each 64-byte chunk is a separate request and response. The library waits for data in 10 ms steps, so the readout doesn't speed up in proportion to the baud rate.

### Image re-encoding

After the camera is powered off, the remote node decodes the camera's JPEG on the ESP32-S3 and encodes it again, smaller. It uses the JPEG decoder/encoder that ship in the Arduino core (`esp_jpg_decode`, `fmt2jpg_cb`), with frames held in PSRAM. Settings in `[loracamera]`:

| Setting | Default | Meaning |
|---|---|---|
| `jpeg_quality` | `30` | `1`–`100`: quality of the re-encoded JPEG, lower is smaller. `0` turns re-encoding off and sends the camera's JPEG unchanged |
| `night_grayscale` | `1` | `1`: send night shots as grayscale JPEGs. `0`: always colour |
| `night_max_chroma` | `12` | A picture whose mean colourfulness (0–255) is at or below this counts as a night shot |

**Night detection.** Under the camera's IR LEDs a picture is almost colourless. For every pixel the firmware takes the spread between the largest and smallest of R, G, B, and averages it over the image ("chroma"). In host tests a colourful synthetic scene scored 44 and a desaturated one 1. Real camera values still need checking. Grayscale JPEGs drop the colour channels entirely: in the same tests the desaturated image came out at 7.5 KB instead of 9.1 KB at quality 30.

**Safe fallback.** The re-encoded image is sent only if it's smaller than the camera's original. If decoding or encoding fails, or the result doesn't fit `MAX_TRANSFER_BUFFER`, the camera's JPEG is sent unchanged (`Sending the camera JPEG unchanged`).

**Log line** on the remote node for each picture:

```
Re-encoded 640x480 grayscale (chroma 3, quality 30): 48123 -> 12345 bytes in 0.90s
```

**Tuning.**
1. Compare uploaded images at a few `jpeg_quality` values. Host tests on a 640×480 synthetic test image (31.7 KB source) gave 10.2 KB at 30, 13.6 KB at 50 and 25.9 KB at 80 (PSNR 36, 38 and 41 dB). Real camera images will differ.
2. Check the logged `chroma` values on real day and night pictures, then set `night_max_chroma` between them. The default `12` hasn't been checked against this camera.

Re-encoding adds roughly 1 s per picture on the ESP32-S3 [estimate]. That's small next to the airtime it saves.

Smaller resolutions and lower quality transfer much faster. See the [image size limits](troubleshooting.md#large-images-are-skipped).

## Adding a new payload type

The transport moves any byte buffer. To send something else, such as sensor logs:

1. **Remote:** in `loop()`, accept a new second request byte next to `0xFC` (picture) and `0xFD` (audio, reserved even when audio is disabled), for example `0xFB`. `0xFE` and `0xC2` are already used, see [protocol.md](protocol.md). Fill `transfer_buffer` and set `content_length` (≤ `MAX_TRANSFER_BUFFER`). The existing code after the capture then announces the packet count and serves the packets.
2. **Local:** send the new byte from `request_transmission()`, which currently sends `0xFC + content_type`. Give it a URL and `Content-Type` in `put_to_server()`.
3. **Server:** `listen.py` takes the file extension from the last three characters of the URL path, so `/data.csv` is saved as `MMDDHHMM.csv`.

Packet formats are listed in [protocol.md](protocol.md).
