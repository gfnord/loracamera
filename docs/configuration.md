# Configuration reference

All settings are compile-time. Change them, rebuild, and reflash. The link settings and protocol constants are shared by every board and live in `lib/Board/Board.h`. Per-board values (pins, TX power, TCXO, transfer buffer size) live in `lib/Board/HeltecV4R8.h` and `lib/Board/HeltecV2.h`; the build picks one by environment. **The options in the `[loracamera]` section of `platformio.ini` (`audio`, `motion`, `poll_interval`, `test_image` and the camera settings) now only affect the remote node: reflash the remote only.** The base station always listens and ignores them. The settings in `lib/Board/Board.h` are shared: after changing one of those, reflash both boards, and both nodes must run the same protocol version. `display` / `display_v2` affect whichever node they're flashed to, `sdcard` only the base station, and the camera settings only the remote node.

| Environment | Board | Node |
|---|---|---|
| `local`, `remote` (the default `pio run` builds only these) | Heltec V4-R8 | base station, field node |
| `local_v2`, `remote_v2` | Heltec WiFi LoRa 32 V2 ([wiring and power](hardware.md#heltec-v2)) | base station, field node |

The boards can be mixed (for example a V2 remote with a V4-R8 local node): the link settings and sync word are the same on air for the SX1262 and the SX1276. The V2 and mixed pairs have only been compiled, **not tested on hardware**. The `[loracamera]` options apply to every environment.

## Motion detection (PIR) and the timer

The remote node deep-sleeps between pictures in both modes. `motion` chooses what wakes it. Set in `platformio.ini`:

```ini
[loracamera]
motion = 1            ; default; 0 = no PIR, wake on a timer instead
poll_interval = 3600  ; motion = 0 only: seconds of sleep between pictures (1-86400)
motion_cooldown = 60  ; motion = 1 only: seconds the PIR stays disarmed after each picture (0-86400)
```

Then rebuild and reflash the **remote** node only; the base station needs nothing. The value reaches the code as `LORACAMERA_MOTION`. `poll_interval` is used only by the remote node, as `LORACAMERA_POLL_INTERVAL`, and only with `motion = 0`. The code default is `600` when it isn't set, and the build fails outside 1–86400 s. `motion_cooldown` is used only with `motion = 1`, as `LORACAMERA_MOTION_COOLDOWN` (code default `60`; the build fails outside 0–86400 s).

| | `motion = 1` (default) | `motion = 0` |
|---|---|---|
| Who starts a transfer | the remote node, always | the remote node, always |
| What wakes the remote | PIR output HIGH | deep-sleep timer, every `poll_interval` seconds |
| Remote at power-on or reset | arms the PIR, takes no picture | takes one picture at once, then sleeps (confirms the link after installing) |
| Remote node between pictures | deep sleep (radio asleep, front end, Vext and camera off) | the same deep sleep |
| Base station | always listens, never transmits on its own | the same |
| PIR wiring | V4-R8: GPIO6 + 3V3 + GND ([hardware.md](hardware.md#motion-sensor-pir)); V2: GPIO32 + 3V3 + GND ([hardware.md](hardware.md#heltec-v2)) | not needed |
| Audio | not available (`audio = 1` fails the build) | optional |

`poll_interval` is the sleep after each capture or transfer ends, so the pictures are a little more than `poll_interval` seconds apart: add the capture and transfer time (about 1 min for a re-encoded picture). What a shorter interval costs in battery is in [hardware.md](hardware.md#power).

What happens on the remote node on each wake-up:

1. **Power-on or reset:** with `motion = 1` it starts the radio, puts it straight back to sleep and goes to deep sleep; no picture is taken until the PIR triggers. With `motion = 0` it takes a picture right away.
2. **PIR output goes HIGH (`motion = 1`) or the timer fires (`motion = 0`):** the chip wakes and restarts. It takes a picture (or, with `audio = 1`, every other time an audio clip), then announces it to the base station: `F9 C2` for a picture, `F9 C3` for audio. Each announcement is sent `ANNOUNCE_REPEAT` (5) times, and repeated every `ANNOUNCE_GAP_MS` (5 s) until the first packet request arrives, at most `ANNOUNCE_TRIES` (6) times. If nobody answers, the capture is dropped (`The base station never answered; picture dropped`). If the camera fails, it logs `Nothing to send`, announces nothing and goes back to sleep.
3. It serves packet requests until the base station sends "done" (`Transfer done`), or until no request has arrived for `SERVE_IDLE_MS` (30 s).
4. It goes back to deep sleep. With `motion = 0`, for `poll_interval` seconds. With `motion = 1`, first for `motion_cooldown` seconds on the timer with the PIR **disarmed** (`Cool-down: PIR disarmed for N s`), so a PIR held HIGH by moving branches or sun-warmed surfaces can't keep the remote taking pictures back to back. When the cool-down ends the remote wakes only briefly (`Cool-down over`): without starting the radio, display or camera, it re-arms the PIR and sleeps again. If the PIR is still HIGH then, the next picture follows at once, so constant triggering gives at most one picture per `motion_cooldown` plus the ~40 s capture and transfer. `motion_cooldown = 0` re-arms the PIR straight after the transfer.

The PIR pin is `MOTION_PIR` in the board header (`lib/Board/HeltecV4R8.h`: GPIO6; `lib/Board/HeltecV2.h`: GPIO32). It must be an RTC GPIO to wake the chip (0–21 on the V4-R8); the build fails otherwise. With `motion = 0` it isn't used.

## Audio capture (optional)

Audio is **off by default**, so the link carries only pictures. Audio needs the timer mode (`motion = 0`), so to build the remote node with microphone support set this in `platformio.ini`:

```ini
[loracamera]
audio = 1
motion = 0
```

Then rebuild and reflash the **remote** node; the base station needs no change. The value reaches the code as the `LORACAMERA_AUDIO` macro (`-DLORACAMERA_AUDIO=${loracamera.audio}`). `audio = 1` with `motion = 1` fails the build (`motion = 1 needs audio = 0`): audio goes with the timer wake-ups, and on the V4-R8 the microphone's data pin is the PIR pin. The V2 mic and PIR use separate pins, but the rule is the same.

| | `audio = 0` (default) | `audio = 1` |
|---|---|---|
| What the remote announces | pictures only (`F9 C2`) | pictures (`F9 C2`) and audio (`F9 C3`), alternating, one per wake-up |
| Remote node | camera only; I2S driver and recording code are not compiled in | also records `AUDIO_SECONDS` of audio on every other wake-up |
| Base station | accepts either announcement | the same; uploads audio to `/audio.raw` and saves `.raw` on the SD card |
| Microphone wiring | not needed | V4-R8: GPIO3/4/6, see [hardware.md](hardware.md#remote-node-wiring). V2: GPIO12/13/39, see [hardware.md](hardware.md#heltec-v2) |
| Uploads | `/image.jpg` | `/image.jpg` and `/audio.raw` |
| Cycle time (SF7) | a 48 KB camera JPEG ≈ 46 s, a re-encoded 10–20 KB one ≈ 10–20 s | adds ≈ 1.8 min of airtime for every 5 s clip (≈ 110 KB) |

The remote starts with a picture after power-on, then alternates picture, audio, picture… one per wake-up. The state is kept in RTC memory, which survives deep sleep, and flips only after a transfer completes: a dropped announcement or a failed capture repeats the same type next time. The base station needs no matching setting: it takes the type from the announcement.

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

Every screen starts with the station's role, centred on the top line: **`- Base Station -`** on the local node, **`- Remote Station -`** on the remote node. Both boards can be the same model, and the role is set only by the firmware flashed (`local*` or `remote*` environment), so this line is how to tell them apart. Up to four status lines follow:

| Node | When | Status lines |
|---|---|---|
| both | boot | `Starting LoRa...` and `Firmware <version>` (`LORACAMERA_VERSION` in `lib/Board/Board.h`), or `Starting LoRa failed!` |
| local | ready | `Ready`, Wi-Fi IP, `Wi-Fi: SSID not found` or `Wi-Fi: not connected`, SD card state (with `sdcard = 1`), `Listening for remote` |
| local | receiving | `Receiving`, `i / n [%]`, RSSI of the last data packet |
| local | after a transfer | `Received`, size and time, `Saved to SD card` / `SD card save failed`, `Uploaded` / `Upload failed` / `Upload skipped: no Wi-Fi`; or `Nothing received` (with `Too big for this board` if the file didn't fit) |
| remote | ready | `Ready`, `LoRa OK`, `Wakes every N s` (`motion = 0`) or `Sleeps until motion` (`motion = 1`) |
| remote | capturing | `Motion detected` (`motion = 1` only), `Taking picture...` (`Test image...` with `test_image = 1`), `Recording audio`; then `Picture ready` with the size, or `No picture` (no camera answer: check the serial log) |
| remote | announcing | `Announcing`, `picture` or `audio`, and the packet count (`N packets`) |
| remote | sending | `Sending`, `i / n`, RSSI of the request (redrawn at most every 250 ms so the drawing doesn't slow the transfer) |
| remote | end | `Transfer done` or `No answer from the base station`; the screen goes dark when Vext is switched off for deep sleep |

The remote node always starts a transfer ([Motion detection](#motion-detection-pir-and-the-timer)): it wakes on the PIR or its timer and announces the capture (`Announcing`), and the base station only listens (`Listening for remote`) and then collects it (`Receiving`).

Vext is switched on either way while the board is awake, because it also powers the `Ve` header pins (V4-R8: GPIO40; V2: GPIO21, LOW = on). On the remote the OLED is only on while the board is awake. Both nodes' screens have run on Heltec V2 boards; the V4-R8 display hasn't been run on hardware.

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
- **Incomplete transfers:** if the remote stopped answering mid-transfer (`Remote stopped answering; abandoning the transfer at packet N`), the truncated file is saved and uploaded as received.
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

Airtime for one full data packet (252 bytes, explicit header, CRC on, CR 4/5). Transfer times also count the request packet and the protocol's turnaround time (see [protocol.md](protocol.md#throughput)), assuming no retries:

| SF / BW | Data packet airtime | ≈ 48 KB JPEG (192 packets) | ≈ 20 KB re-encoded JPEG (80 packets) | ≈ 5 s audio (442 packets) |
|---|---|---|---|---|
| SF7 / 250 kHz (default) | 0.20 s | 46 s | 19 s | 1.8 min |
| SF9 / 250 kHz | 0.63 s | 2.3 min | 57 s | 5.2 min |
| SF10 / 125 kHz | 2.3 s | 8.0 min | 3.3 min | 18 min |

These are calculated with the Semtech airtime formula, not measured.

**Limit:** the base station waits `RESPONSE_TIMER` × 20 ms ≈ 4 s for each data packet. The remote node's 20 ms loop delay and 5 ms turnaround guard plus the data packet airtime must fit in that window. That rules out SF11 and SF12 at 125 kHz (≈ 4.9 s and 9 s per packet) unless you lengthen the window by raising the `delay(20)` inside the response loop of `handle_packets()`. `RESPONSE_TIMER` is a `uint8_t` and can't go above 255. RadioLib enables low-data-rate optimisation automatically where the radio needs it (SF11/SF12 at 125 kHz).

## Transfer protocol

Shared constants are in `lib/Board/Board.h`; `MAX_TRANSFER_BUFFER` is per board. The packet formats and the timing are in [protocol.md](protocol.md).

| Constant | Default | Meaning |
|---|---|---|
| `MAX_TRANSFER_BUFFER` | V4-R8 `125000` (500 packets), V2 `72000` (288 packets) | Size of the capture/receive buffer on each node (bytes), set in the board header. Captures must fit. Must be a multiple of `LORA_TRANSFER_BUFFER` (250), checked at build time. The V2's is smaller because with the V4-R8 size its local node overflowed the ESP32's internal RAM by 48800 bytes |
| `LORA_TRANSFER_BUFFER` | `250` | Payload bytes per data packet. Each packet also carries a 2-byte header, and the LoRa maximum is 255 |
| `PKT_MAGIC` | `0xF9` | First byte of every control packet |
| `PKT_ANNOUNCE_PICTURE` / `PKT_ANNOUNCE_AUDIO` | `0xC2` / `0xC3` | Second byte of the remote's 4-byte announcement: `F9 C2 nL nH` (picture) or `F9 C3 nL nH` (audio) |
| `PKT_REQUEST` | `0xFE` | Second byte of the base station's packet request, `F9 FE iL iH` |
| `PKT_DONE` | `0xFA` | Second byte of the base station's "done", `F9 FA` |
| `ANNOUNCE_REPEAT` / `ANNOUNCE_REPEAT_GAP_MS` | `5` / `20` | The remote sends each announcement this many times, this far apart. The base station waits out the repeats (from the radio's actual airtime) before its first packet request |
| `ANNOUNCE_GAP_MS` / `ANNOUNCE_TRIES` | `5000` / `6` | The remote repeats its announcement this often, this many times, until the first packet request, then drops the capture and sleeps. Covers a base station that is booting, saving or uploading |
| `PACKET_GIVE_UP_MS` | `20000` | The base station abandons the transfer when one packet hasn't arrived after this long (the remote has probably gone back to sleep) and keeps what it has |
| `SERVE_IDLE_MS` | `30000` | The remote goes back to sleep after this long without a packet request. Must exceed `PACKET_GIVE_UP_MS` (checked at build time) |
| `DONE_REPEAT` | `3` | The base station sends "done" this many times, `ANNOUNCE_REPEAT_GAP_MS` apart |

**Buffers that differ between the two nodes.** If a remote announces more packets than fit the base station's buffer (`packets × 250 > MAX_TRANSFER_BUFFER`, for example a V4-R8 remote with a big picture and a V2 base station), the base station logs `N packets don't fit this board's M-byte buffer; skipped` and requests nothing. It still sends "done", so the remote goes to sleep. A V2 base station handles V4-R8 remotes as long as their pictures stay under 72000 bytes (re-encoded pictures are 10–20 KB).

## Local node (`src/local/main.cpp`)

| Constant | Default | Meaning |
|---|---|---|
| `SERVER_URL` | from `auth.h` | Upload target, `http://` only, without a trailing slash. Pictures are PUT to `/image.jpg`, and audio (if enabled) to `/audio.raw`. Non-2xx responses and connection errors are logged as `Upload failed: …`; the file isn't retried (it's on the SD card with `sdcard = 1`) |
| `UPLOAD_TOKEN` | from `auth.h` | Sent as `Authorization: Bearer <token>`; must match the server's `UPLOAD_TOKEN` ([usage.md](usage.md#1-run-the-upload-server)), or uploads fail with `401` |
| `WIFI_SSID`, `WIFI_PASS` | from `auth.h` | 2.4 GHz network |
| `RESPONSE_TIMER` | `200` | Polls (20 ms each) waiting for each data packet, ≈ 4 s. Must be ≤ 255. A packet that doesn't arrive within `PACKET_GIVE_UP_MS` ends the transfer |
| `WIFI_CONNECT_TIMEOUT_MS` | `20000` | How long `setup()` waits for Wi-Fi before starting without it. The driver keeps reconnecting in the background, and uploads are skipped (`Wi-Fi not connected, upload skipped`) until it's up |

The base station takes the content type from the announcement (`F9 C2` = picture, `F9 C3` = audio) and logs `Picture announced, N packets` or `Audio announced, N packets`. It ignores `motion`, `poll_interval`, `audio` and `test_image`: it only listens, so its firmware needs reflashing only when the protocol or its own settings (Wi-Fi, SD card, display) change.

## Remote node (`src/remote/main.cpp` and the board header)

### Pins

Defined in the board header (`lib/Board/HeltecV4R8.h` or `lib/Board/HeltecV2.h`), not in `src/remote/main.cpp`:

| Define | V4-R8 | V2 | Function |
|---|---|---|---|
| `CAMERATX_BOARDRX` | 47 | 23 | UART1 RX (camera TX) |
| `CAMERARX_BOARDTX` | 48 | 17 | UART1 TX (camera RX) |
| `CAMERA_ONOFF` | 41 | 22 | Camera power control, HIGH = on. Held LOW through deep sleep. On the V2 it isn't GPIO2 (a strapping pin) |
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

Each capture then skips the camera entirely (it stays switched off) and loads a built-in 640×480 JPEG test card, 22704 bytes (91 packets), from `src/remote/test_image.h`. The picture then follows the camera picture's path: re-encoded on a V4-R8 with `jpeg_quality` > 0, sent unchanged on a V2. The remote logs `test_image = 1: using the built-in 22704-byte test JPEG, camera not used` and shows `Test image...`. With `motion = 0` the test card goes out right after power-on and on every timer wake-up. With `motion = 1` each PIR wake-up sends it; bridging the PIR pin to 3V3 with a jumper simulates motion (not yet run on hardware). Only the remote node uses it, so changing it needs only the remote reflashed; set it back to `0` and reflash the remote before fitting the camera. To use another picture, regenerate the header from any JPEG up to 72000 bytes: `xxd -i < picture.jpg`, wrapped in `static const uint8_t TEST_JPEG[] = { … };`.

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

The transport moves any byte buffer. To send something else, such as sensor logs (the remote always starts the transfer and picks the type):

1. **Remote:** pick a new announcement type byte next to `PKT_ANNOUNCE_PICTURE` (`0xC2`) and `PKT_ANNOUNCE_AUDIO` (`0xC3`), for example `0xC4` (`F9 C4 nL nH`); `0xF9`, `0xFA` and `0xFE` are already used, see [protocol.md](protocol.md). In `pushCapture()`, fill `transfer_buffer`, set `content_length` (≤ `MAX_TRANSFER_BUFFER`) and pass the new byte to `announce()`. The existing code then announces the packet count and serves the packets.
2. **Base station:** in `loop()`, accept the new byte in the announcement check and set `content_type` from it. Map the type to a URL, `Content-Type` and file extension in `put_to_server()` (and in the `storageSave()` call in `handle_packets()` for the SD card file name).
3. **Server:** `loracamera-server` takes the file extension from the last three characters of the URL path (letters and digits only), so `/data.csv` is saved as `MMDDHHMM.csv`. Raise its `MAX_UPLOAD_BYTES` if the payload can exceed 262144 bytes.

Packet formats are listed in [protocol.md](protocol.md).
