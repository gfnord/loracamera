# Troubleshooting

## First bring-up checklist

Go through these on the bench, with the boards a few metres apart. At very close range a strong signal can overload the receiver (the V4-R8's front end adds a +21 dB LNA).

1. **Remote radio.** Flash `remote` (V2: `remote_v2`), open `pio device monitor`, press RST. Expect `I have … free heap` and no `Starting LoRa failed!`. With `motion = 1` (default) it then logs `Sleeping until the PIR on GPIO6 sees motion` (GPIO32 on a V2) and, on a V4-R8, its USB port disappears; that's expected. With `motion = 0` it takes a picture right away, then logs `Sleeping N s until the next picture` and its USB port disappears too. On a V2 remote the log also says `No PSRAM on this board: jpeg_quality is ignored, camera JPEGs are sent unchanged`; that's expected too.
2. **Local radio, SD card and Wi-Fi.** Flash `local` (V2: `local_v2`) with a valid `auth.h`. The log must show no `Starting LoRa failed!`, then `SD card: … MB free, next file …` (with `sdcard = 1`) and `Wi-Fi connected, IP …`. With the display on, the OLED (top line `- Base Station -`) ends on `Ready` with the Wi-Fi and SD card state, or shows `Starting LoRa failed!`.
3. **Server.** `curl -X PUT` a file with the bearer token to the upload server from another machine (see [usage.md](usage.md#1-run-the-upload-server)); expect `Saved "…"`.
4. **Link.** Power the base station first or second, either works. With `motion = 0` the remote pushes a picture right after power-on: within ~30 s the local log shows `Picture announced, N packets`, then packet progress with RSSI. With `motion = 1`, wave a hand in front of the PIR: within ~30 s the same line appears.
5. **Camera.** The remote log shows `reset with 1` and `len of frame is …`. With `camera_type = maix` it first shows `Camera: Maix (K210) via VC0706 emulation`; run the Maix tests in [maix.md](maix.md) first.
6. **Audio** (only with `audio = 1`, `motion = 0`). The remote alternates picture and audio, one per wake-up, so the second wake-up (`poll_interval` seconds after the first) sends audio and the local log shows `Audio announced, N packets`. Convert the `.raw` file and listen to it.
7. **SD card.** After the first transfer the local log shows `Saved /loracam/000001.jpg (… bytes)`. Check the file on a computer.

## Build and flash

### `auth.h: No such file or directory`
Create `src/local/auth.h` (see [getting-started.md](getting-started.md#wi-fi-credentials-required)).

### Upload fails with `Failed to connect to ESP32-S3` or `No serial data received`
- V4-R8: enter download mode by hand: hold **PRG**, press **RST**, release **PRG**, then retry. Press RST after flashing. A remote node is in deep sleep with its USB port gone almost all the time (in both `motion` modes), so it always needs this (or a wake-up at the right moment).
- V2: the CP2102 should reset the board into download mode itself. Check the USB cable (some are charge-only), the port (`/dev/ttyUSB0`) and, on Windows, the CP210x driver. If it still fails, use the PRG/RST sequence above.
- Under WSL2, a V4-R8 re-enumerates when it enters the bootloader. Re-run `usbipd attach --wsl --busid <BUSID>`, or use `--auto-attach`. A V2 (CP2102) needs only the one-time attach.
- With two boards connected, pass `--upload-port /dev/ttyACMx` (V2: `/dev/ttyUSBx`).

### `Permission denied: '/dev/ttyACM0'` (or `/dev/ttyUSB0` on a V2)
Add yourself to `dialout`, or install the PlatformIO udev rules (see [getting-started.md](getting-started.md#linux-permissions)).

### Serial monitor shows nothing
On a V4-R8 the board uses native USB CDC, so output written before the host opens the port is lost. Press RST with the monitor running. After an upload, reopen the monitor. A V4-R8 remote node only has a USB port while it's awake (power-on, then each PIR trigger or timer wake-up); `pio device monitor` reconnects when it comes back, and under WSL2 `usbipd attach --auto-attach` re-attaches it. A V2 talks through the CP2102, so its port stays present while the board sleeps; pass `-p /dev/ttyUSB0` if PlatformIO picks the wrong port.

## Radio

### `LoRa init failed: <code>`

| Code | RadioLib meaning | Likely cause |
|---|---|---|
| `-2` | `ERR_CHIP_NOT_FOUND` | No SPI response: wrong board or a damaged SX1262 (V4-R8) / SX1276 (V2) |
| `-705` / `-707` | SPI command timeout / failed | V4-R8 (SX1262): BUSY pin stuck. Power-cycle the board |
| `-703` | Invalid TCXO voltage | V4-R8 only (the V2 has no TCXO setting). `LORA_TCXO_VOLTAGE` was changed. It must stay `1.8` |
| `-12`, `-8`, `-9`, `-13` | Invalid frequency / bandwidth / SF / power | A link value in `lib/Board/Board.h`, or the TX power in the board header (`lib/Board/HeltecV4R8.h` / `lib/Board/HeltecV2.h`), is out of range |

The full list is in RadioLib's `src/TypeDef.h`.

### No pictures arrive: the local node never logs `Picture announced`
- Is the remote node powered and does its log show the radio started? Does it wake (see below)? With `motion = 0`, it should push a picture right after power-on, and then every `poll_interval` seconds; a long `poll_interval` (the `platformio.ini` default is 3600) looks like nothing happening.
- Both nodes must be flashed from the **same** `lib/Board/Board.h` (frequency, BW, SF, CR, sync word) and run the same protocol version: compare the `Firmware <version>` line on both boot screens (serial: `loracamera … station, firmware <version>`). Firmware before 0.2.0, whose base station polled with request-for-transmission packets, never answers the remote's announcements. Mixed V4-R8 / V2 pairs share these settings; the sync word `0x12` is the same on air for the SX1262 and SX1276. Mixed pairs are untested on hardware.
- The remote logs `Announcing picture: N bytes in M packets` and then `The base station never answered; picture dropped` after ~30 s: the base station wasn't listening or the link is down. Check the antennas on both boards, and that the base station finished booting (it logs `Wi-Fi connected …` or the Wi-Fi problem, then listens).
- The remote logs `Nothing to send`: the capture failed (camera or buffer), so nothing was announced. See [Remote node](#remote-node).
- A picture can take ~25 s to capture; the remote announces only after that. Nothing is heard on the link before then.
- Does the remote wake? Watch its monitor (it reconnects on each wake-up on a V4-R8) or its current draw. With `motion = 1`, wave at the PIR: no wake means checking the PIR's 3V3 and GND (`3V3`, not `Ve`, which is off during sleep), and that OUT goes to GPIO6 (GPIO32 on a V2) and reads ~3.3 V while triggered. Some PIRs need up to a minute after power-up before they trigger. The V2's deep-sleep current has not been measured; don't assume the V4-R8 figures. With `motion = 0` the PIR isn't used.
- PIR sensors don't see through glass or clear plastic. Mount the lens outside the enclosure.

### `motion = 1`: pictures all the time
- Something keeps the PIR output HIGH: moving branches, sun-warmed or wind-blown surfaces, a road, an animal sitting in view. The remote takes the next picture as soon as each `motion_cooldown` ends (log: `Cool-down over`, then `Motion detected`) while the output stays HIGH. Re-aim or mask the PIR lens, shorten its range, or raise `motion_cooldown` ([configuration.md](configuration.md#motion-detection-pir-and-the-timer)).
- With nothing connected to GPIO6 (GPIO32 on a V2) the internal pull-down keeps the board asleep. If it wakes with no PIR fitted, look for a wiring fault pulling that pin up.
- A picture or two right after power-up is the PIR settling.

### `Remote stopped answering; abandoning the transfer at packet N`
The local node gives up after 20 s (`PACKET_GIVE_UP_MS`) without the requested packet, saves what it has, and sends "done". The link failed mid-transfer: see "Transfers crawl" below.

### Transfers crawl: many `response timer timeout` or `LoRa receive error: -7`
`-7` is a CRC error: the packet arrived corrupted. Both messages point to a weak or noisy link:
- Watch the RSSI in the local node's serial log (`rssi: …` on each progress line), or on either node's OLED with the display on (the remote shows the RSSI of each request). The SX1262's sensitivity at SF7/250 kHz is around −120 dBm. Readings within a few dB of that mean the link is marginal.
- Raise `LORA_SF` (see [configuration.md](configuration.md#range-vs-speed)), and keep the 4 s response-window limit in mind.
- Improve antenna placement before raising TX power.

### `Mismatched packet` now and then
This is harmless. A late reply to an earlier request arrived, and the local node asks again.

### TX works but the other side never receives
V4-R8 only: its KCT8103L front end must be powered and switched. `radioBegin()` drives GPIO7 and GPIO2 HIGH, and `radioSend()` drives GPIO5 HIGH during each transmission. If you reused any of GPIO2, 5 or 7 for something else, transmission breaks. The V2 has no external front end (SX1276 on PA_BOOST), so this doesn't apply; if a V2 can't transmit, check the antenna and the LoRa init result.

## Local node

### `Wi-Fi not connected yet` / `Wi-Fi not connected, upload skipped`
The base station waits `WIFI_CONNECT_TIMEOUT_MS` (20 s) at boot, then runs without Wi-Fi and keeps retrying in the background. Pictures still go to the SD card. The boot log names the reason:
- `SSID not found (2.4 GHz only) (status 1)`: no 2.4 GHz network with exactly the name in `WIFI_SSID` (case-sensitive) is in range. Dual-band routers often use one name for both bands or a separate name for 2.4 GHz; a 5 GHz-only network is invisible to the ESP32. Check the router's 2.4 GHz network name; a phone or PC scan shows which names are broadcast on 2.4 GHz.
- `connect failed (password?) (status 4)`: the network was found but refused the connection. Check `WIFI_PASS`, and that the network uses WPA2 (or WPA2/WPA3 mixed), not WPA3-only.
- Anything else: out of range or the router isn't answering.

### `SD card not found`
- The card must be **FAT32**. 64 GB+ cards come formatted exFAT, which this firmware can't read: reformat them.
- Check the wiring against [hardware.md](hardware.md#base-station-wiring): CS GPIO4, SCK GPIO39, MISO GPIO38, MOSI GPIO41, power from 3V3. On a V2 ([hardware.md](hardware.md#heltec-v2)): CS GPIO23, SCK GPIO22, MISO GPIO33, MOSI GPIO17, power from 3V3.
- Breakouts with an AMS1117 regulator need 5 V and won't run from 3V3.
- V4-R8: nothing plugged into the GNSS connector: it shares GPIO38/39/41.
- The firmware retries before every file, so fixing the card doesn't need a reboot.

### `SD card: can't open …` / `wrote N of M bytes`
The card was removed or failed during a write. The base station remounts before the next file. If it repeats, replace the card.

### `SD card full`
Copy the files off and delete them, or use a bigger card. New files are still uploaded if Wi-Fi is up.

### `N packets don't fit this board's M-byte buffer; skipped`
The picture announced by the remote is larger than the local board's transfer buffer (`MAX_TRANSFER_BUFFER`: 125000 bytes on a V4-R8, 72000 on a V2). The local node requests nothing, and still sends "done" so the remote sleeps. It only happens with a mixed pair: a V4-R8 remote sending more than 72000 bytes to a V2 local node. Keep `jpeg_quality` enabled on the V4-R8 remote (re-encoded pictures are 10–20 KB) or raise `camera_compression` so the camera JPEG is smaller, or use a V4-R8 as the local node.

### OLED stays black
- The OLED is only driven when the firmware is built with the display on: `display = 1` for V4-R8 environments (default `0`, for boards without a display), `display_v2 = 1` for V2 environments (the default).
- The OLED is powered from Vext (GPIO40 on a V4-R8, active **LOW**) and reset through GPIO21. On a V2, Vext is GPIO21 (active **LOW**) and reset is GPIO16 (SDA 4, SCL 15). Both are handled by `vextOn()` and `oledReset()`.
- Every screen starts with the role, `- Base Station -` or `- Remote Station -`, from power-up on. The remote's screen goes dark when it goes to deep sleep: that's expected.

### Uploads never appear on the server
- With `sdcard = 1` every picture is also on the card, so nothing is lost; copy the missed ones from `/loracam`.
- The local node logs `Upload failed: <status or error>` when the server doesn't answer with 2xx, and `Wi-Fi not connected, upload skipped` without Wi-Fi. Watch the server log (`docker logs -f loracamera-server`) for `"PUT /image.jpg" 201 Saved "…"` lines.
- `Upload failed: 401`: `UPLOAD_TOKEN` in `src/local/auth.h` doesn't match the server's `UPLOAD_TOKEN`. `413`: the file exceeds the server's `MAX_UPLOAD_BYTES`.
- `SERVER_URL` must be `http://` (the firmware doesn't do TLS): the server's LAN IP and port 8000, or a host name that Caddy serves over plain HTTP. Behind Cloudflare the DNS record must be DNS only: a proxied record that redirects to HTTPS fails. Test it with `curl` from another device.
- Server inside WSL2: see [usage.md](usage.md#running-the-server-under-wsl2).
- A server on your LAN: check its firewall for inbound TCP 8000.

## Remote node

### `Turning on camera; reset with 0` / `Couldn't take pic`
- With `camera_type = maix`, see [maix.md](maix.md#troubleshooting) (`Maix didn't answer within 10000 ms` and the Maix's own log).
- Check the camera supply: it needs **5 V** (up to 325 mA with the IR LEDs on), and GPIO41 (GPIO22 on a V2) must actually switch it. On a LiPo the board has no 5 V rail (see [hardware.md](hardware.md#power)).
- TX and RX are crossed: camera TX goes to GPIO47 and camera RX to GPIO48. On a V2: camera TX goes to GPIO23 and camera RX to GPIO17.
- The camera must power up at 38400 baud, its factory default. The firmware never changes the saved speed. If the camera's EEPROM was set to another baud by other software, set it back with that tool.
- The VC0706 library is timing-sensitive: the serial calls were slowed down on purpose. Leave the `delay()`s in `takePicture()`/`turnOnCamera()` in place.

### `Camera didn't answer at 115200 baud, staying at 38400`
Pictures still work, just at the slower speed. Check for long or unshielded camera wires, which matter more at 115200. If it happens every time, set `camera_baud = 38400` in `platformio.ini` to skip the switch.

### Pictures are blocky, or still large
- With re-encoding on (the default on a V4-R8), `jpeg_quality` sets the size: lower is smaller and blockier. Compare the `Re-encoded … -> N bytes` lines in the remote log.
- With `jpeg_quality = 0`, `camera_compression` is the only size control: higher means smaller and blockier. Watch `len of frame is …`.
- A V2 remote never re-encodes (no PSRAM), so `camera_compression` is the only size control there too.
- A lower camera resolution (`VC0706_320x240` in `turnOnCamera()`) cuts size the most. See [configuration.md](configuration.md#image-re-encoding).

### `JPEG decode failed` / `JPEG encode failed` / `Sending the camera JPEG unchanged`
Re-encoding failed, or wouldn't have made the image smaller, so the camera's original JPEG was sent. Nothing is lost. A one-off decode failure usually means a camera read went wrong. `out of PSRAM` means the board's PSRAM isn't working, which is unexpected on a V4-R8 built from this project. A V2 has no PSRAM and never re-encodes, so it never logs these lines.

### `No PSRAM on this board: jpeg_quality is ignored, camera JPEGs are sent unchanged`
Expected on a V2 remote, logged at boot. `jpeg_quality` and `night_grayscale` have no effect there: the camera JPEG is sent as is. Re-encoding needs a full-frame pixel buffer (640×480×3 ≈ 920 KB) that only fits in the V4-R8's PSRAM. To shrink the pictures, raise `camera_compression`.

### Night pictures still in colour (or day pictures in gray)
Night detection compares each picture's mean colourfulness with `night_max_chroma` (default `12`). The remote log prints it on every picture: `Re-encoded 640x480 colour (chroma 15, …)`. Set `night_max_chroma` between the values you see for real night and day shots, then reflash the remote node.

### No pictures at night
The IR LEDs switch on by themselves when the camera's photocell sees darkness. Lit IR LEDs add 250 mA at 5 V, and a weak 5 V supply or boost converter can brown out the camera exactly then. Check that the supply holds up under 325 mA.

### `problem reading data from camera` repeats forever
The camera stopped answering mid-image, and `takePicture()` retries indefinitely. Power-cycle the remote node and check the camera wiring and supply.

### Large images are skipped
- Frames over **256 KB** (`CAMERA_JPEG_MAX`) don't fit the camera buffer: the remote logs `Can't read a … frame` and `Nothing to send`, and announces nothing. On a V2 the camera buffer is smaller: `Can't read a N-byte frame into the 72000-byte camera buffer`. Raise `camera_compression` (the V2 sends the camera JPEG unchanged).
- After re-encoding, an image must fit **125,000 bytes** (`MAX_TRANSFER_BUFFER`; 72,000 on a V2). Otherwise the camera's JPEG is used if it fits, and if neither fits nothing is sent (`doesn't fit the … transfer buffer`).

The VC0706 library's old 16-bit read offset, which corrupted images over 64 KB, has been widened to 32 bits.

### Audio is silent or noise
Audio exists only in builds with `audio = 1` (and `motion = 0`) in `platformio.ini`, on the **remote** node. The base station needs no setting: it takes the type from the announcement.
- The firmware reads `I2S_CHANNEL_FMT_ONLY_RIGHT`. The original ESP32 swapped left and right in this mode, and the ESP32-S3 may not. If the file is silent, change it to `I2S_CHANNEL_FMT_ONLY_LEFT` in `src/remote/main.cpp`, or tie the mic's SEL pin to 3V3.
- Check BCLK → GPIO4, LRCL → GPIO3, DOUT → GPIO6, and that the mic has 3.3 V. On a V2: BCLK → GPIO13, LRCL → GPIO12, DOUT → GPIO39. GPIO12 is a strapping pin: nothing may pull it HIGH at reset.
- Too quiet or clipping: adjust `GAIN_FACTOR`.
- V2 remote: `Audio buffer full (72000 bytes), clip cut short` is expected. The V2's 72000-byte transfer buffer is smaller than a full 5 s clip (110250 bytes), so recording stops when the buffer is full and the clip is about 3.2 s long. V2 audio is untested on hardware.
- Make sure the file is decoded as s16le, 11025 Hz, mono (see [usage.md](usage.md#audio)).
