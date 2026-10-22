# Configuration reference

All settings are compile-time. Change them, rebuild, and reflash. The link settings and protocol constants are shared by every board and live in `lib/Board/Board.h`. Per-board values (pins, TX power, TCXO, transfer buffer size) live in `lib/Board/HeltecV4R8.h` and `lib/Board/HeltecV2.h`; the build picks one by environment. **`audio` and `motion` in the `[loracamera]` section of `platformio.ini` and the settings in `lib/Board/Board.h` are shared: reflash both boards after changing any of them.** `display` / `display_v2` affect whichever node they're flashed to, `sdcard` only the local node, and the camera settings only the remote node.

| Environment | Board | Node |
|---|---|---|
| `local`, `remote` (the default `pio run` builds only these) | Heltec V4-R8 | base station, field node |
| `local_v2`, `remote_v2` | Heltec WiFi LoRa 32 V2 ([wiring and power](hardware.md#heltec-v2)) | base station, field node |

The boards can be mixed (for example a V2 remote with a V4-R8 local node): the link settings and sync word are the same on air for the SX1262 and the SX1276. The V2 and mixed pairs have only been compiled, **not tested on hardware**. The `[loracamera]` options apply to every environment.

## Motion detection (PIR)

Motion detection is **on by default**: the remote node sleeps and takes a picture only when its PIR sensor sees movement. Set in `platformio.ini`:

```ini
[loracamera]
motion = 1          ; default; 0 = no PIR, the local node polls for a picture
poll_interval = 10  ; motion = 0 only: seconds between the end of one transfer and the next request
```

Then rebuild and reflash **both** boards: the two values use different protocols ([protocol.md](protocol.md#motion-mode-motion--1)). The value reaches the code as `LORACAMERA_MOTION`. `poll_interval` (0–86400) is used only by the local node, so changing it needs only the base station reflashed; it reaches the code as `LORACAMERA_POLL_INTERVAL` and sets `CHECK_EVERY_MS` in `src/local/main.cpp`. After a request that brings no transfer back (no answer, or the camera failed), the interval counts from when that request was sent.

| | `motion = 1` (default) | `motion = 0` |
|---|---|---|
| Who starts a transfer | the remote node, when the PIR triggers | the local node, `poll_interval` seconds after the last transfer |
| Remote node between pictures | deep sleep (radio asleep, front end, Vext and camera off) | awake in receive mode (~75 mA) |
| Local node | listens for announcements, never transmits on its own | sends a request `poll_interval` seconds after the last transfer |
| PIR wiring | V4-R8: GPIO6 + 3V3 + GND ([hardware.md](hardware.md#motion-sensor-pir)); V2: GPIO32 + 3V3 + GND ([hardware.md](hardware.md#heltec-v2)) | not needed |
| Audio | not available (`audio = 1` fails the build) | optional |

What happens on the remote node with `motion = 1`:

1. **Power-on or reset:** it starts the radio, puts it straight back to sleep and goes to deep sleep. No picture is taken until the PIR triggers.
2. **PIR output goes HIGH:** the chip wakes and restarts. It takes a picture (same capture and re-encoding as before), then announces it to the local node, repeating the announcement every `MOTION_ANNOUNCE_GAP_MS` (5 s) until the first packet request arrives, at most `MOTION_ANNOUNCE_TRIES` (6) times. If nobody answers, the picture is dropped.
3. It serves packet requests until the local node sends "done", or until no request has arrived for `MOTION_SERVE_IDLE_MS` (30 s).
4. It goes back to deep sleep. If the PIR output is still HIGH, it wakes again at once and takes the next picture.

The PIR pin is `MOTION_PIR` in the board header (`lib/Board/HeltecV4R8.h`: GPIO6; `lib/Board/HeltecV2.h`: GPIO32). It must be an RTC GPIO to wake the chip (0–21 on the V4-R8); the build fails otherwise.

## Audio capture (optional)

Audio is **off by default**, so the link carries only pictures. Audio needs the polling mode, so to build both nodes with microphone support set this in `platformio.ini`:

```ini
[loracamera]
audio = 1
motion = 0
```

Then rebuild and reflash **both** boards. The value reaches the code as the `LORACAMERA_AUDIO` macro (`-DLORACAMERA_AUDIO=${loracamera.audio}`). `audio = 1` with `motion = 1` fails the build (`motion = 1 needs audio = 0`): the remote node has no way to announce audio, and on the V4-R8 the microphone's data pin is the PIR pin. The V2 mic and PIR use separate pins, but the rule is the same.

| | `audio = 0` (default) | `audio = 1` |
|---|---|---|
| Local node requests | pictures only | pictures and audio, alternating |
| Remote node | camera only; I2S driver and recording code are not compiled in; `F9 FD` requests are ignored | also records `AUDIO_SECONDS` of audio on `F9 FD` |
| Microphone wiring | not needed | V4-R8: GPIO3/4/6, see [hardware.md](hardware.md#remote-node-wiring). V2: GPIO12/13/39, see [hardware.md](hardware.md#heltec-v2) |
| Uploads | `/image.jpg` | `/image.jpg` and `/audio.raw` |
| Cycle time (SF7) | a 48 KB camera JPEG ≈ 46 s, a re-encoded 10–20 KB one ≈ 10–20 s | adds ≈ 1.8 min of airtime for every 5 s clip (≈ 110 KB) |

Both nodes must be built with the same value. A local node built with audio sends `F9 FD` requests that a picture-only remote node never answers. The local node then waits out its retries (up to ~100 min) instead of fetching pictures.

The default 5 s clip is 110250 bytes: it fits the V4-R8 buffer (125000) but not the V2 buffer (72000). On a V2 remote the recording stops when the transfer buffer is full and logs `Audio buffer full (72000 bytes), clip cut short`, so the clip is cut at 72000 bytes (≈ 3.2 s at 11025 Hz); nothing overruns memory. To avoid the cut, set `AUDIO_SECONDS` so that `SAMPLE_RATE × AUDIO_SECONDS × 2` stays below 72000 (3 s = 66150 bytes). Audio on the V2 is untested.

## OLED display

Both nodes can show their status on the 0.96" OLED. There's one setting per board type, because the V4-R8 is sold with or without the OLED and every V2 has one:

```ini
[loracamera]
display = 0      ; V4-R8 environments (local, remote); default 0
display_v2 = 1   ; V2 environments (local_v2, remote_v2); default 1
```

Rebuild and reflash the boards of that type. The value reaches the code as `LORACAMERA_DISPLAY`; the drawing code is in `lib/Display/`.

| | display off | display on |
|---|---|---|
| Status | serial log only | serial log and OLED |
| OLED driver (ThingPulse SSD1306) | not compiled in | linked; resets the OLED and drives it over I2C: V4-R8 reset GPIO21, I2C GPIO17/18; V2 reset GPIO16, I2C SDA GPIO4 / SCL GPIO15 |
| OLED pins | V4-R8: GPIO17, 18, 21 free. V2: GPIO4, 15, 16 free | V4-R8: GPIO17, 18, 21 used. V2: GPIO4, 15, 16 used |

What the screens show:

| Node | When | Screen |
|---|---|---|
| both | boot | `Base station` / `Remote node`, `Starting LoRa...`, or `Starting LoRa failed!` |
| local | ready | `Base station ready`, Wi-Fi IP, `Wi-Fi: SSID not found` or `Wi-Fi: not connected`, SD card state (with `sdcard = 1`), `Waiting for motion` or `Polling the remote` |
| local | `motion = 0`, asking | `Requesting picture` (or `audio`), attempt number |
| local | receiving | `Receiving`, `i / n [%]`, RSSI of the last data packet |
| local | after a transfer | `Received`, size and time, `Saved to SD card` / `SD card save failed`, `Uploaded` / `Upload failed` / `Upload skipped: no Wi-Fi`; or `Nothing received` (with `Too big for this board` if the file didn't fit) |
| remote | ready | `LoRa OK`, `Waiting for requests` (`motion = 0`) or `Sleeps until motion` |
| remote | capturing | `Motion detected`, `Taking picture...` (`Test image...` with `test_image = 1`), `Recording audio`; then `Picture ready` with the size, or `No picture` (no camera answer: check the serial log) |
| remote | sending | `Announcing` and the packet count; then `Sending`, `i / n`, RSSI of the request (redrawn at most every 250 ms so the drawing doesn't slow the transfer) |
| remote | `motion = 1`, end | `Transfer done` or `No answer from the base station`; the screen goes dark when Vext is switched off for deep sleep |

Vext is switched on either way while the board is awake, because it also powers the `Ve` header pins (V4-R8: GPIO40; V2: GPIO21, LOW = on). On a `motion = 1` remote the OLED is only on while the board is awake. The remote's OLED code hasn't been run on hardware.

## SD card (base station)

The base station saves every file it receives to a microSD card, **before** trying to upload it. Set in `platformio.ini` (local node only):

```ini
[loracamera]
sdcard = 1   ; default; 0 = upload only, SD code not compiled in
```

| | `sdcard = 0` | `sdcard = 1` (default) |
|---|---|---|
| Received files | uploaded only | saved to `/loracam/NNNNNN.jpg` (or `.raw`), then uploaded |
| Without Wi-Fi | files are lost | files are kept on the card |
| SD card SPI pins | free (V4-R8: GPIO4, 38, 39, 41; V2: GPIO17, 22, 23, 33) | SD card SPI bus ([V4-R8 wiring](hardware.md#base-station-wiring), [V2 wiring](hardware.md#heltec-v2)). V4-R8: CS 4, SCK 39, MISO 38, MOSI 41. V2: CS 23, SCK 22, MISO 33, MOSI 17 |

How it behaves:
- **Card format:** FAT32 only. The ESP32 core used here has exFAT disabled, so 64 GB+ (SDXC) cards must be reformatted.
- **File names:** `000001.jpg`, `000002.jpg` … in `/loracam`. At boot the firmware continues after the highest number already on the card, so reboots never overwrite files.
- **Timestamps:** once Wi-Fi has been up, the clock is set from NTP (`pool.ntp.org`, UTC), and every file's modification time on the card is correct. If Wi-Fi hasn't been up since boot, files carry an arbitrary date; the numbering still gives the order.
- **Missing or removed card:** the base station keeps working and logs `SD card not found`. Each new file retries the mount, so a card inserted later is picked up without a reboot.
- **Full card:** the file isn't saved (`SD card full`), and it's still uploaded if Wi-Fi is up.
- **Incomplete transfers:** if a packet was skipped after all retries, the shorter, damaged file is saved and uploaded as received, same as before.
- **Wiring:** the SD pins are `SD_CS`, `SD_SCK`, `SD_MISO` and `SD_MOSI` in the board header (`lib/Board/HeltecV4R8.h` or `lib/Board/HeltecV2.h`).

## Radio link

Shared by every board, in `lib/Board/Board.h`:

| Constant | Default | Meaning |
|---|---|---|
| `LORA_FREQ_MHZ` | `915.0` | Carrier frequency. The V4-R8's HTIT-WBR8H covers 863–928 MHz |
| `LORA_BW_KHZ` | `250.0` | LoRa bandwidth |
| `LORA_SF` | `7` | Spreading factor (7–12). Higher means more range and slower transfers |
| `LORA_CR` | `5` | Coding rate denominator, i.e. 4/5 |
| `LORA_SYNC_WORD` | `0x12` | Private-network sync word. Change it on both nodes to ignore other LoRa traffic |
| `LORA_PREAMBLE` | `8` | Preamble length in symbols |

Per board, in the board header:

| Constant | V4-R8 (`lib/Board/HeltecV4R8.h`) | V2 (`lib/Board/HeltecV2.h`) | Meaning |
|---|---|---|---|
| `LORA_TCXO_VOLTAGE` | `1.8` | none (not used) | Board-specific, don't change |
| `LORA_TX_POWER_DBM` | `7` | `17` | Chip output power, see below. SX1262 on the V4-R8 (−9 to 22), SX1276 PA_BOOST on the V2 |
| `LORA_CURRENT_LIMIT_MA` | n/a | `100` | V2: over-current limit. RadioLib's 60 mA default would clip 17 dBm (~90 mA) |

RadioLib also enables a 2-byte payload CRC. Packets that fail it are dropped and requested again.

### Choosing the frequency

In Canada and the US use the 902–928 MHz ISM band. 915 MHz sits in the middle of it. Pick any value inside the band, set the same value on both nodes, and stay clear of the band edges with 250 kHz bandwidth.

This firmware transmits on a single fixed channel with no frequency hopping. Make sure that is acceptable for your use under ISED RSS-247 (Canada) or FCC Part 15.247 (US).

### TX power

`LORA_TX_POWER_DBM` sets the chip output. **V4-R8:** the KCT8103L amplifier adds gain on top of it, and the gain shrinks as the input rises. Measured gain, from Meshtastic's KCT8103L table:

| `LORA_TX_POWER_DBM` | Front-end gain | ≈ Antenna power |
|---|---|---|
| 0 | +13 dB | 13 dBm |
| 7 (default) | +13 dB | 20 dBm (same as the original V2 build) |
| 13 | +13 dB | 26 dBm |
| 17 | +11 dB | 28 dBm |
| 21 | +7 dB | 28 dBm |

Above ~17 the output saturates near Heltec's 28 ± 1 dBm rating, while current draw keeps rising (~750 mA at 27 dBm). Raising SF usually buys more range per milliamp than raising power. Antenna gain adds to EIRP, which is the figure regulators limit.

**V2:** the SX1276 drives the antenna through PA_BOOST with no external amplifier or front end.

| `LORA_TX_POWER_DBM` | Antenna power | Note |
|---|---|---|
| 17 (default) | ≈ 17 dBm | PA_BOOST maximum for continuous use |
| 20 | ≈ 20 dBm | Rated only for 1 % duty cycle, which picture transfers exceed. The original V2 build used 20 dBm |

At its default the V2 transmits about 3 dB less than a V4-R8 at its default (≈ 20 dBm at the antenna). Keep `LORA_CURRENT_LIMIT_MA` at 100 or above: with RadioLib's 60 mA default the 17 dBm output would be clipped. Range on a V2 link has not been measured.

### Range vs. speed

Airtime for one full data packet (252 bytes, explicit header, CRC on, CR 4/5). Transfer times also count the request packet and the protocol's polling and turnaround time (see [protocol.md](protocol.md#throughput)), assuming no retries:

| SF / BW | Data packet airtime | ≈ 48 KB JPEG (192 packets) | ≈ 20 KB re-encoded JPEG (80 packets) | ≈ 5 s audio (442 packets) |
|---|---|---|---|---|
| SF7 / 250 kHz (default) | 0.20 s | 46 s | 19 s | 1.8 min |
| SF9 / 250 kHz | 0.63 s | 2.3 min | 57 s | 5.2 min |
| SF10 / 125 kHz | 2.3 s | 8.0 min | 3.3 min | 18 min |

These are calculated with the Semtech airtime formula, not measured.

**Limit:** the local node waits `RESPONSE_TIMER` × 20 ms ≈ 4 s for each data packet. The remote node's poll interval and 5 ms turnaround guard plus the data packet airtime must fit in that window. That rules out SF11 and SF12 at 125 kHz (≈ 4.9 s and 9 s per packet) unless you lengthen the window by raising the `delay(20)` inside the response loop of `handle_packets()`. `RESPONSE_TIMER` is a `uint8_t` and can't go above 255. RadioLib enables low-data-rate optimisation automatically where the radio needs it (SF11/SF12 at 125 kHz).

## Transfer protocol

Shared constants are in `lib/Board/Board.h`; `MAX_TRANSFER_BUFFER` is per board.

| Constant | Default | Meaning |
|---|---|---|
| `MAX_TRANSFER_BUFFER` | V4-R8 `125000` (500 packets), V2 `72000` (288 packets) | Size of the capture/receive buffer on each node (bytes), set in the board header. Captures must fit. Must be a multiple of `LORA_TRANSFER_BUFFER` (250), checked at build time. The V2's is smaller because with the V4-R8 size its local node overflowed the ESP32's internal RAM by 48800 bytes |
| `LORA_TRANSFER_BUFFER` | `250` | Payload bytes per data packet. Each packet also carries a 2-byte header, and the LoRa maximum is 255 |
| `RFT_RESPONSE_REPEAT` / `RFT_RESPONSE_GAP_MS` | `5` / `20` | The remote sends its "N packets" reply this many times, this far apart. The local node waits out the repeats (from the radio's actual airtime) before its first packet request |
| `MOTION_ANNOUNCE_GAP_MS` / `MOTION_ANNOUNCE_TRIES` | `5000` / `6` | `motion = 1`: the remote repeats its announcement this often, this many times, until the first packet request. Covers a local node that is booting, saving or uploading |
| `MOTION_PACKET_GIVE_UP_MS` | `20000` | `motion = 1`: the local node abandons the transfer when one packet hasn't arrived after this long (the remote has probably gone back to sleep) and keeps what it has |
| `MOTION_SERVE_IDLE_MS` | `30000` | `motion = 1`: the remote goes back to sleep after this long without a packet request. Must exceed `MOTION_PACKET_GIVE_UP_MS` (checked at build time) |
| `MOTION_DONE_REPEAT` | `3` | `motion = 1`: the local node sends "done" this many times, `RFT_RESPONSE_GAP_MS` apart |

**Buffers that differ between the two nodes.** If a remote announces more packets than fit the local node's buffer (`packets × 250 > MAX_TRANSFER_BUFFER`, for example a V4-R8 remote with a big picture and a V2 local node), the local node logs `N packets don't fit this board's M-byte buffer; skipped` and requests nothing. With `motion = 1` it still sends "done", so the remote goes to sleep. A V2 local node handles V4-R8 remotes as long as their pictures stay under 72000 bytes (re-encoded pictures are 10–20 KB).

## Local node (`src/local/main.cpp`)

| Constant | Default | Meaning |
|---|---|---|
| `SERVER_URL` | from `auth.h` | Upload target, `http://` only, without a trailing slash. Pictures are PUT to `/image.jpg`, and audio (if enabled) to `/audio.raw`. Non-2xx responses and connection errors are logged as `Upload failed: …`; the file isn't retried (it's on the SD card with `sdcard = 1`) |
| `UPLOAD_TOKEN` | from `auth.h` | Sent as `Authorization: Bearer <token>`; must match the server's `UPLOAD_TOKEN` ([usage.md](usage.md#1-run-the-upload-server)), or uploads fail with `401` |
| `WIFI_SSID`, `WIFI_PASS` | from `auth.h` | 2.4 GHz network |
| `CHECK_EVERY_MS` | `poll_interval` × 1000 (10000) | `motion = 0` only. Pause after a finished transfer before the next request. Set `poll_interval` in `platformio.ini` (see [Motion detection](#motion-detection-pir)) |
| `MAX_RFT_RESPONSE_MS` | `30000` | `motion = 0` only. How long to wait for the remote node to capture and answer a request. Must exceed the remote's capture time: picture ≈ 2.5 s of fixed delays + the image read (≈ 20 s at 640×480 and 38400 baud, per the original code comment; less at 115200) + 1 s power-off + re-encoding (≈ 1 s [estimate]); audio (if enabled) = `AUDIO_SECONDS` |
| `REQUEST_TIMER` | `200` | Retries for a request, and per data packet (with `motion = 1` also capped by `MOTION_PACKET_GIVE_UP_MS`). Must fit in a `uint8_t` (≤ 255) |
| `RESPONSE_TIMER` | `200` | Polls (20 ms each) waiting for each data packet, ≈ 4 s. Must be ≤ 255 |
| `WIFI_CONNECT_TIMEOUT_MS` | `20000` | How long `setup()` waits for Wi-Fi before starting without it. The driver keeps reconnecting in the background, and uploads are skipped (`Wi-Fi not connected, upload skipped`) until it's up |

With `motion = 1` (default), every transfer is a **picture** the remote node announced. With `motion = 0` and the default `audio = 0`, every request is for a picture. With `audio = 1`, the first request after boot is for a picture, and the type then alternates between picture and audio after each completed transfer.

## Remote node (`src/remote/main.cpp` and the board header)

### Pins

Defined in the board header (`lib/Board/HeltecV4R8.h` or `lib/Board/HeltecV2.h`), not in `src/remote/main.cpp`:

| Define | V4-R8 | V2 | Function |
|---|---|---|---|
| `CAMERATX_BOARDRX` | 47 | 23 | UART1 RX (camera TX) |
| `CAMERARX_BOARDTX` | 48 | 17 | UART1 TX (camera RX) |
| `CAMERA_ONOFF` | 41 | 22 | Camera power control, HIGH = on. Held LOW through deep sleep with `motion = 1`. On the V2 it isn't GPIO2 (a strapping pin) |
| `MOTION_PIR` | 6 | 32 | PIR output, HIGH = motion (`motion = 1` only). Must be an RTC GPIO. V2: internal pull-down, power the PIR from 3V3 |
| `I2S_BCK` | 4 | 13 | Mic bit clock (`audio = 1` only) |
| `I2S_LRC` | 3 | 12 | Mic word select (`audio = 1` only). V2: GPIO12 is a strapping pin, nothing may pull it HIGH at reset |
| `I2S_DIN` | 6 | 39 | Mic data (`audio = 1` only). V2: input-only pin |

V4-R8: pick replacements only from the free pins listed in [hardware.md](hardware.md#v4-r8-pin-map): 3, 4, 6, 15, 16, 38, 39, 41, 42, 47, 48, and for `MOTION_PIR` only from 3, 4, 6, 15, 16 (RTC GPIOs). Avoid 45 and 46, which are strapping pins, and 43/44, where the ROM bootloader prints at boot. V2: see [hardware.md](hardware.md#heltec-v2) for the pin map and the pins to avoid.

### Audio

These apply only when built with `audio = 1` (see [Audio capture](#audio-capture-optional)).

| Constant | Default | Meaning |
|---|---|---|
| `SAMPLE_RATE` | `11025` | Hz |
| `AUDIO_SECONDS` | `5` | Recording length. Recording stops early once `SAMPLE_RATE × AUDIO_SECONDS × 2` bytes would exceed `MAX_TRANSFER_BUFFER` (125000 on the V4-R8, 72000 on the V2: ≈ 3.2 s) |
| `GAIN_FACTOR` | `2` | Each 32-bit I2S sample is shifted right by `12 − GAIN_FACTOR` before truncating to 16 bits. +1 doubles the volume |
| `AUDIO_FRAME_SIZE` | `1024` | I2S DMA buffer and read size, in bytes |

The output is mono signed 16-bit little-endian PCM (s16le). Default: 5 s × 11025 Hz × 2 B ≈ 110 KB.

### Camera

These camera settings live in the `[loracamera]` section of `platformio.ini`. They apply to V4-R8 and V2 remotes alike:

| Setting | Default | Meaning |
|---|---|---|
| `camera_baud` | `115200` | Serial speed for the picture readout: `38400` or `115200` (the VC0706 maximum). Other values fail the build |
| `camera_compression` | `54` | VC0706 JPEG compression register, `0`–`255`. `54` (0x36) is the camera's own default; higher means smaller, blockier JPEGs. Out-of-range values fail the build |

How they're applied, on every picture, in `turnOnCamera()`:

1. The camera powers up at 38400 baud. The baud change isn't saved on the camera; its saved speed lives in its EEPROM, which the firmware never writes. `resetCamera()` resets it at 38400. If that gets no answer (the camera's power wasn't actually cut since the last picture), it retries at `camera_baud`.
2. `setCompression(camera_compression)` writes the compression register. It's a volatile chip register, so it's set after the reset.
3. `speedUpCamera()` switches the camera and `Serial1` to `camera_baud`, then checks the link with a version request. On success the remote logs `Camera serial at 115200 baud`. Otherwise it logs `Camera didn't answer at 115200 baud, staying at 38400` and reads the picture at 38400.

Picking `camera_compression`:
- **With re-encoding on (`jpeg_quality` > 0, the default; V4-R8 only):** the camera's JPEG only crosses the serial cable and is then re-compressed, so keep it at `54` for a clean source. Raising it saves serial readout time, but compressing twice adds artifacts.
- **With re-encoding off (`jpeg_quality = 0`, and always on a V2 remote):** this is the only size control. Raise it until the image gets too blocky (watch `len of frame is …` in the remote log), then back off. On a V2 remote, frames over 72000 bytes are dropped (`Can't read a N-byte frame into the 72000-byte camera buffer`), so raise it if you see that.

Fixed camera settings in `src/remote/main.cpp`:

| Setting | Default | Where |
|---|---|---|
| Resolution | `VC0706_640x480` | `turnOnCamera()`; alternatives `VC0706_320x240`, `VC0706_160x120` |
| Chunk size | 64 bytes | `takePicture()`; the camera buffer is 100 bytes |
| Camera buffer | `CAMERA_JPEG_MAX` | V4-R8: 256 KB PSRAM buffer the camera's JPEG is read into. Larger frames are skipped. V2: no PSRAM, the JPEG is read straight into the transfer buffer, so the limit is `MAX_TRANSFER_BUFFER` (72000) |

Each 64-byte chunk is a separate request and response. The library waits for data in 10 ms steps, so the readout doesn't speed up in proportion to the baud rate.

### Test image (no camera)

To test the link, the SD card and the upload without a camera, build the **remote** node with:

```ini
[loracamera]
test_image = 1   ; default 0
```

Each capture then skips the camera entirely (it stays switched off) and loads a built-in 640×480 JPEG test card, 22704 bytes (91 packets), from `src/remote/test_image.h`. The picture then follows the camera picture's path: re-encoded on a V4-R8 with `jpeg_quality` > 0, sent unchanged on a V2. The remote logs `test_image = 1: using the built-in 22704-byte test JPEG, camera not used` and shows `Test image...`. With `motion = 0` every request gets the test card (run on two V2s). With `motion = 1` each PIR wake-up sends it; bridging the PIR pin to 3V3 with a jumper simulates motion (not yet run on hardware). Only the remote node uses it; set it back to `0` and reflash the remote before fitting the camera. To use another picture, regenerate the header from any JPEG up to 72000 bytes: `xxd -i < picture.jpg`, wrapped in `static const uint8_t TEST_JPEG[] = { … };`.

### Image re-encoding

**Heltec V2 remotes do not re-encode.** The V2 has no PSRAM, and re-encoding needs a full-frame pixel buffer (640×480×3 ≈ 920 KB) that only fits in PSRAM. On a V2 remote `jpeg_quality`, `night_grayscale` and `night_max_chroma` are ignored: the camera's JPEG is sent unchanged, and the boot log says `No PSRAM on this board: jpeg_quality is ignored, camera JPEGs are sent unchanged`. Camera JPEGs larger than 72000 bytes are dropped; raise `camera_compression` to shrink them (see [Camera](#camera)). The rest of this section describes the V4-R8.

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

The transport moves any byte buffer. To send something else, such as sensor logs (with `motion = 0`, where the local node asks for each payload):

1. **Remote:** in `loop()`, accept a new second request byte next to `0xFC` (picture) and `0xFD` (audio, reserved even when audio is disabled), for example `0xFB`. `0xFA`, `0xFE` and `0xC2` are already used, see [protocol.md](protocol.md). Fill `transfer_buffer` and set `content_length` (≤ `MAX_TRANSFER_BUFFER`). The existing code after the capture then announces the packet count and serves the packets.
2. **Local:** send the new byte from `request_transmission()`, which currently sends `0xFC + content_type`. Give it a URL and `Content-Type` in `put_to_server()`.
3. **Server:** `loracamera-server` takes the file extension from the last three characters of the URL path (letters and digits only), so `/data.csv` is saved as `MMDDHHMM.csv`. Raise its `MAX_UPLOAD_BYTES` if the payload can exceed 262144 bytes.

Packet formats are listed in [protocol.md](protocol.md).
