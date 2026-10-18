# Troubleshooting

## First bring-up checklist

Go through these on the bench, with the boards a few metres apart. At very close range a strong signal can overload the receiver, which has a +21 dB LNA.

1. **Remote radio.** Flash `remote`, open `pio device monitor`, press RST. Expect `I have … free heap` and no `LoRa init failed`.
2. **Local display and radio.** Flash `local` with a valid `auth.h`. The OLED must not show `Starting LoRa failed!`, and the log must reach `I have … free heap`, which means Wi-Fi joined.
3. **Server.** `curl -X PUT` a file to `listen.py` from another machine (see [usage.md](usage.md#1-run-the-upload-server)).
4. **Link.** With both boards running, the local log shows `Receiving rft response, N packets` within ~30 s, then packet progress with RSSI.
5. **Camera.** The remote log shows `reset with 1` and `len of frame is …`.
6. **Audio** (only with `audio = 1`). After the first picture, the next cycle records audio. Convert the `.raw` file and listen to it.

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
- Watch the RSSI on the OLED. The SX1262's sensitivity at SF7/250 kHz is around −120 dBm. Readings within a few dB of that mean the link is marginal.
- Raise `LORA_SF` (see [configuration.md](configuration.md#range-vs-speed)), and keep the 4 s response-window limit in mind.
- Improve antenna placement before raising TX power.

### `Mismatched packet` now and then
This is harmless. A late reply to an earlier request arrived, and the local node asks again.

### TX works but the other side never receives
On this board the KCT8103L front end must be powered and switched. `radioBegin()` drives GPIO7 and GPIO2 HIGH, and `radioSend()` drives GPIO5 HIGH during each transmission. If you reused any of GPIO2, 5 or 7 for something else, transmission breaks.

## Local node

### Local node hangs after boot
`setup()` waits forever for Wi-Fi without logging anything. Check `WIFI_SSID`/`WIFI_PASS` in `auth.h`, and make sure the network is 2.4 GHz and in range.

### OLED stays black
- The OLED is powered from Vext (GPIO40, active **LOW**) and reset through GPIO21. Both are handled by `vextOn()` and `oledReset()`.
- It's normal for the screen to stay blank until the first packet arrives.

### Uploads never appear on the server
- The local node doesn't check the HTTP result. Watch the `listen.py` console for `"PUT /image.jpg HTTP/1.1" 201` lines.
- `SERVER_URL` must use the server's LAN IP and port 8000. Test it with `curl` from another device on the same network.
- Server inside WSL2: see [usage.md](usage.md#running-the-server-under-wsl2).
- Check the firewall on the server machine for inbound TCP 8000.

## Remote node

### `Turning on camera; reset with 0` / `Couldn't take pic`
- Check the camera supply: GPIO41 must actually switch power to the camera.
- TX and RX are crossed: camera TX goes to GPIO47 and camera RX to GPIO48.
- The camera must run at 38400 baud, its factory default.
- The VC0706 library is timing-sensitive: the serial calls were slowed down on purpose. Leave the `delay()`s in `takePicture()`/`turnOnCamera()` in place.

### `problem reading data from camera` repeats forever
The camera stopped answering mid-image, and `takePicture()` retries indefinitely. Power-cycle the remote node and check the camera wiring and supply.

### Large images are corrupted
Two limits from the original code apply:
- The VC0706 library keeps its read offset in a 16-bit counter (`frameptr`). Images larger than **65,535 bytes** wrap around and come out corrupted.
- `takePicture()` doesn't check the frame length against `MAX_TRANSFER_BUFFER` (125,000 bytes).

If `len of frame is` reports more than 65535, switch to `VC0706_320x240` in `turnOnCamera()`.

### Audio is silent or noise
Audio exists only in builds with `audio = 1` in `platformio.ini`, on **both** nodes.
- The firmware reads `I2S_CHANNEL_FMT_ONLY_RIGHT`. The original ESP32 swapped left and right in this mode, and the ESP32-S3 may not. If the file is silent, change it to `I2S_CHANNEL_FMT_ONLY_LEFT` in `src/remote/main.cpp`, or tie the mic's SEL pin to 3V3.
- Check BCLK → GPIO4, LRCL → GPIO3, DOUT → GPIO6, and that the mic has 3.3 V.
- Too quiet or clipping: adjust `GAIN_FACTOR`.
- Make sure the file is decoded as s16le, 11025 Hz, mono (see [usage.md](usage.md#audio)).
