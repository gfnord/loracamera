# Troubleshooting

## First bring-up checklist

Go through these on the bench, with the boards a few metres apart. At very close range a strong signal can overload the receiver, which has a +21 dB LNA.

1. **Remote radio.** Flash `remote`, open `pio device monitor`, press RST. Expect `I have … free heap` and no `LoRa init failed`.
2. **Local radio, SD card and Wi-Fi.** Flash `local` with a valid `auth.h`. The log must show no `Starting LoRa failed!`, then `SD card: … MB free, next file …` (with `sdcard = 1`) and `Wi-Fi connected, IP …`. With `display = 1` the OLED shows the same LoRa failure message.
3. **Server.** `curl -X PUT` a file to `listen.py` from another machine (see [usage.md](usage.md#1-run-the-upload-server)).
4. **Link.** With both boards running, the local log shows `Receiving rft response, N packets` within ~30 s, then packet progress with RSSI.
5. **Camera.** The remote log shows `reset with 1` and `len of frame is …`.
6. **Audio** (only with `audio = 1`). After the first picture, the next cycle records audio. Convert the `.raw` file and listen to it.
7. **SD card.** After the first transfer the local log shows `Saved /loracam/000001.jpg (… bytes)`. Check the file on a computer.

## Build and flash

### `auth.h: No such file or directory`
Create `src/local/auth.h` (see [getting-started.md](getting-started.md#wi-fi-credentials-required)).

### Upload fails with `Failed to connect to ESP32-S3` or `No serial data received`
- Enter download mode by hand: hold **PRG**, press **RST**, release **PRG**, then retry. Press RST after flashing.
- Under WSL2, the device re-enumerates when it enters the bootloader. Re-run `usbipd attach --wsl --busid <BUSID>`, or use `--auto-attach`.
- With two boards connected, pass `--upload-port /dev/ttyACMx`.

### `Permission denied: '/dev/ttyACM0'`
Add yourself to `dialout`, or install the PlatformIO udev rules (see [getting-started.md](getting-started.md#linux-permissions)).

### Serial monitor shows nothing
The board uses native USB CDC, so output written before the host opens the port is lost. Press RST with the monitor running. After an upload, reopen the monitor.

## Radio

### `LoRa init failed: <code>`

| Code | RadioLib meaning | Likely cause |
|---|---|---|
| `-2` | `ERR_CHIP_NOT_FOUND` | No SPI response: wrong board or a damaged SX1262 |
| `-705` / `-707` | SPI command timeout / failed | BUSY pin stuck. Power-cycle the board |
| `-703` | Invalid TCXO voltage | `LORA_TCXO_VOLTAGE` was changed. It must stay `1.8` |
| `-12`, `-8`, `-9`, `-13` | Invalid frequency / bandwidth / SF / power | A value in `HeltecV4R8.h` is out of range |

The full list is in RadioLib's `src/TypeDef.h`.

### No `rft response`: the local node keeps printing `Receive timer timed out`
- Is the remote node powered and does its log show the radio started?
- Both nodes must be flashed from the **same** `HeltecV4R8.h` (frequency, BW, SF, CR, sync word).
- If the request log says `type 1`, the local node was built with `audio = 1`. A remote node built with `audio = 0` ignores audio requests. Use the same `audio` value in `platformio.ini` for both boards.
- A picture can take ~25 s to capture. The local node waits `MAX_RFT_RESPONSE_MS` (30 s). If you raised the resolution or slowed the camera, raise that limit too.
- Check the antennas on both boards.

### Transfers crawl: many `response timer timeout` or `LoRa receive error: -7`
`-7` is a CRC error: the packet arrived corrupted. Both messages point to a weak or noisy link:
- Watch the RSSI in the local node's serial log (`rssi: …` on each progress line), or on the OLED with `display = 1`. The SX1262's sensitivity at SF7/250 kHz is around −120 dBm. Readings within a few dB of that mean the link is marginal.
- Raise `LORA_SF` (see [configuration.md](configuration.md#range-vs-speed)), and keep the 4 s response-window limit in mind.
- Improve antenna placement before raising TX power.

### `Mismatched packet` now and then
This is harmless. A late reply to an earlier request arrived, and the local node asks again.

### TX works but the other side never receives
On this board the KCT8103L front end must be powered and switched. `radioBegin()` drives GPIO7 and GPIO2 HIGH, and `radioSend()` drives GPIO5 HIGH during each transmission. If you reused any of GPIO2, 5 or 7 for something else, transmission breaks.

## Local node

### `Wi-Fi not connected yet` / `Wi-Fi not connected, upload skipped`
The base station waits `WIFI_CONNECT_TIMEOUT_MS` (20 s) at boot, then runs without Wi-Fi and keeps retrying in the background. Pictures still go to the SD card. Check `WIFI_SSID`/`WIFI_PASS` in `auth.h`, and make sure the network is 2.4 GHz and in range.

### `SD card not found`
- The card must be **FAT32**. 64 GB+ cards come formatted exFAT, which this firmware can't read: reformat them.
- Check the wiring against [hardware.md](hardware.md#base-station-wiring): CS GPIO4, SCK GPIO39, MISO GPIO38, MOSI GPIO41, power from 3V3.
- Breakouts with an AMS1117 regulator need 5 V and won't run from 3V3.
- Nothing plugged into the GNSS connector: it shares GPIO38/39/41.
- The firmware retries before every file, so fixing the card doesn't need a reboot.

### `SD card: can't open …` / `wrote N of M bytes`
The card was removed or failed during a write. The base station remounts before the next file. If it repeats, replace the card.

### `SD card full`
Copy the files off and delete them, or use a bigger card. New files are still uploaded if Wi-Fi is up.

### OLED stays black
- The OLED is only driven when the firmware is built with `display = 1` in `platformio.ini`. The default is `0`, for boards without a display.
- The OLED is powered from Vext (GPIO40, active **LOW**) and reset through GPIO21. Both are handled by `vextOn()` and `oledReset()`.
- It's normal for the screen to stay blank until the first packet arrives.

### Uploads never appear on the server
- With `sdcard = 1` every picture is also on the card, so nothing is lost; copy the missed ones from `/loracam`.
- The local node logs `Upload failed: <status or error>` when the server doesn't answer with 2xx, and `Wi-Fi not connected, upload skipped` without Wi-Fi. Watch the `listen.py` console for `"PUT /image.jpg HTTP/1.1" 201` lines.
- `SERVER_URL` must use the server's LAN IP and port 8000. Test it with `curl` from another device on the same network.
- Server inside WSL2: see [usage.md](usage.md#running-the-server-under-wsl2).
- Check the firewall on the server machine for inbound TCP 8000.

## Remote node

### `Turning on camera; reset with 0` / `Couldn't take pic`
- Check the camera supply: it needs **5 V** (up to 325 mA with the IR LEDs on), and GPIO41 must actually switch it. On a LiPo the board has no 5 V rail (see [hardware.md](hardware.md#power)).
- TX and RX are crossed: camera TX goes to GPIO47 and camera RX to GPIO48.
- The camera must power up at 38400 baud, its factory default. The firmware never changes the saved speed. If the camera's EEPROM was set to another baud by other software, set it back with that tool.
- The VC0706 library is timing-sensitive: the serial calls were slowed down on purpose. Leave the `delay()`s in `takePicture()`/`turnOnCamera()` in place.

### `Camera didn't answer at 115200 baud, staying at 38400`
Pictures still work, just at the slower speed. Check for long or unshielded camera wires, which matter more at 115200. If it happens every time, set `camera_baud = 38400` in `platformio.ini` to skip the switch.

### Pictures are blocky, or still large
- With re-encoding on (the default), `jpeg_quality` sets the size: lower is smaller and blockier. Compare the `Re-encoded … -> N bytes` lines in the remote log.
- With `jpeg_quality = 0`, `camera_compression` is the only size control: higher means smaller and blockier. Watch `len of frame is …`.
- A lower camera resolution (`VC0706_320x240` in `turnOnCamera()`) cuts size the most. See [configuration.md](configuration.md#image-re-encoding).

### `JPEG decode failed` / `JPEG encode failed` / `Sending the camera JPEG unchanged`
Re-encoding failed, or wouldn't have made the image smaller, so the camera's original JPEG was sent. Nothing is lost. A one-off decode failure usually means a camera read went wrong. `out of PSRAM` means the board's PSRAM isn't working, which is unexpected on a V4-R8 built from this project.

### Night pictures still in colour (or day pictures in gray)
Night detection compares each picture's mean colourfulness with `night_max_chroma` (default `12`). The remote log prints it on every picture: `Re-encoded 640x480 colour (chroma 15, …)`. Set `night_max_chroma` between the values you see for real night and day shots, then reflash the remote node.

### No pictures at night
The IR LEDs switch on by themselves when the camera's photocell sees darkness. Lit IR LEDs add 250 mA at 5 V, and a weak 5 V supply or boost converter can brown out the camera exactly then. Check that the supply holds up under 325 mA.

### `problem reading data from camera` repeats forever
The camera stopped answering mid-image, and `takePicture()` retries indefinitely. Power-cycle the remote node and check the camera wiring and supply.

### Large images are skipped
- Frames over **256 KB** (`CAMERA_JPEG_MAX`) don't fit the camera buffer: the remote logs `Can't read a … frame` and answers 0 packets.
- After re-encoding, an image must fit **125,000 bytes** (`MAX_TRANSFER_BUFFER`). Otherwise the camera's JPEG is used if it fits, and if neither fits nothing is sent (`doesn't fit the … transfer buffer`).

The VC0706 library's old 16-bit read offset, which corrupted images over 64 KB, has been widened to 32 bits.

### Audio is silent or noise
Audio exists only in builds with `audio = 1` in `platformio.ini`, on **both** nodes.
- The firmware reads `I2S_CHANNEL_FMT_ONLY_RIGHT`. The original ESP32 swapped left and right in this mode, and the ESP32-S3 may not. If the file is silent, change it to `I2S_CHANNEL_FMT_ONLY_LEFT` in `src/remote/main.cpp`, or tie the mic's SEL pin to 3V3.
- Check BCLK → GPIO4, LRCL → GPIO3, DOUT → GPIO6, and that the mic has 3.3 V.
- Too quiet or clipping: adjust `GAIN_FACTOR`.
- Make sure the file is decoded as s16le, 11025 Hz, mono (see [usage.md](usage.md#audio)).
