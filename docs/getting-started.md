# Getting started: configure, build, flash

The project is a [PlatformIO](https://platformio.org/) project using the Arduino framework. One `platformio.ini` defines one environment per node role and board:

| Environment | Board | Node | Source |
|---|---|---|---|
| `local` | Heltec V4-R8 | Near Wi-Fi, the base station; always listens, receives the pictures the remote sends and uploads them | `src/local/` |
| `remote` | Heltec V4-R8 | In the field; deep-sleeps, wakes on a PIR or a timer to take a picture and push it (+ microphone if audio is enabled, with `motion = 0`) | `src/remote/` |
| `local_v2` | Heltec WiFi LoRa 32 V2 | Same as `local` | `src/local/` |
| `remote_v2` | Heltec WiFi LoRa 32 V2 | Same as `remote` | `src/remote/` |

All four are built from the shared library `lib/Board` (`lib/Board/Board.h` selects `lib/Board/HeltecV4R8.h` or `lib/Board/HeltecV2.h` by build flag). Plain `pio run` builds only the V4-R8 pair (`local`, `remote`). The two nodes can use different boards (for example a V2 remote with a V4-R8 local node): the link settings are the same. The V2 environments and mixed pairs compile but have not been tested on hardware. A V2 has no PSRAM, so a V2 remote doesn't re-encode pictures and its picture buffer is 72000 bytes (V4-R8: 125000); see [hardware.md](hardware.md#heltec-v2) for its wiring.

## 1. Install PlatformIO

Any of these works (Linux, macOS, or WSL2):

```sh
uv tool install platformio      # or
pipx install platformio
```

Check it with `pio --version`.

The first build downloads the toolchain and libraries automatically:

| Package | Version (from `platformio.ini`) |
|---|---|
| `platformio/espressif32` platform | ^7.1.3 (Arduino-ESP32 core 2.0.17, ESP-IDF 4.4) |
| `jgromes/RadioLib` | ^7.8.1 |
| `thingpulse/ESP8266 and ESP32 OLED driver for SSD1306 displays` | ^4.6.2 (both nodes; only linked with the display on) |

If you'd rather use an IDE, the PlatformIO extension for VS Code opens this folder directly.

## 2. Configure

### Wi-Fi credentials (required)

Create `src/local/auth.h`. Git ignores it, so your password and upload token stay local:

```c
#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"
#define SERVER_URL "http://192.168.0.150:8000"
#define UPLOAD_TOKEN "the server's UPLOAD_TOKEN"
```

The `local` and `local_v2` environments won't compile without this file, and stop with an `#error` if `SERVER_URL` or `UPLOAD_TOKEN` is missing. The ESP32-S3 (V4-R8) and the ESP32 (V2) only support 2.4 GHz Wi-Fi.

### Upload server address and token (required)

`SERVER_URL` is the upload server ([usage.md](usage.md#1-run-the-upload-server)), without a trailing slash: `http://<LAN IP>:8000` for a server on your network, or `http://loracam.nobrega.com.br` for the production server. It must be `http://`: the firmware doesn't do TLS. `UPLOAD_TOKEN` must equal the server's `UPLOAD_TOKEN`; the firmware sends it as `Authorization: Bearer …` with every upload. If the server runs inside WSL2, see [usage.md](usage.md#running-the-server-under-wsl2).

### Motion detection (on by default)

The remote node deep-sleeps between pictures and starts every transfer itself; the base station only listens. By default (`motion = 1`) a PIR sensor on GPIO6 (GPIO32 on the V2) wakes it when it sees movement. Wire the PIR as in [hardware.md](hardware.md#motion-sensor-pir) (V2: [hardware.md](hardware.md#heltec-v2)). Without a PIR, set `motion = 0` in the `[loracamera]` section of `platformio.ini`: the remote then takes a picture right after power-on and wakes on a timer every `poll_interval` seconds (1–86400). Only the **remote** needs reflashing after changing `motion` or `poll_interval`. See [configuration.md](configuration.md#motion-detection-pir-and-the-timer).

### Audio capture (optional, off by default)

The firmware sends pictures only. To also capture microphone audio, set `audio = 1` and `motion = 0` in the `[loracamera]` section of `platformio.ini`, wire the microphone, and flash the **remote**; the base station needs no change. See [configuration.md](configuration.md#audio-capture-optional).

### Everything else

Radio settings, TX power, pins and timing have working defaults. Link settings live in one shared header (`lib/Board/Board.h`), so both nodes always match, whichever board they use. The `[loracamera]` section of `platformio.ini` (it applies to every environment) holds the project options. `motion`, `poll_interval`, `audio`, `test_image` and the camera options only change the remote node: reflash the remote only. Both nodes must run the same protocol version (a base station on older firmware won't answer the remote's announcements).

| Option | Default | See |
|---|---|---|
| `motion`, `poll_interval`, `motion_cooldown` | `1` (PIR fitted), `3600` (used only with `motion = 0`), `60` (used only with `motion = 1`) | [Motion detection](configuration.md#motion-detection-pir-and-the-timer) |
| `audio` | `0` | [Audio capture](configuration.md#audio-capture-optional) |
| `display`, `display_v2` | `0` (V4-R8: no OLED), `1` (V2: OLED on) | [OLED display](configuration.md#oled-display) |
| `sdcard` | `1` (SD card on the base station) | [SD card](configuration.md#sd-card-base-station) |
| `camera_baud`, `camera_compression` | `115200`, `54` | [Camera](configuration.md#camera) |
| `jpeg_quality`, `night_grayscale`, `night_max_chroma` | `30`, `1`, `12` (ignored on a V2 remote: no PSRAM) | [Image re-encoding](configuration.md#image-re-encoding) |

Everything else is described in [configuration.md](configuration.md).

## 3. Build

```sh
pio run                                   # builds the V4-R8 environments (local, remote)
pio run -e local        # one environment
pio run -e remote
pio run -e local_v2 -e remote_v2          # the Heltec V2 environments
pio run -e local -e remote -e local_v2 -e remote_v2   # all four
```

Output goes to `.pio/build/<env>/firmware.bin`. The build uses:

| Setting | V4-R8 (`local`, `remote`) | V2 (`local_v2`, `remote_v2`) |
|---|---|---|
| `board` | `esp32-s3-devkitc-1`, with overrides for the V4-R8 | `heltec_wifi_lora_32_V2` (ESP32) |
| Flash | 16 MB QIO, `default_16MB.csv` partition table (6.25 MB app slot) | 8 MB, `default_8MB.csv` partition table (3.2 MB app slot) |
| PSRAM | `qio_opi` memory type for the 8 MB octal PSRAM | none |
| `Serial` | `ARDUINO_USB_CDC_ON_BOOT=1`, so `Serial` goes to the native USB port | through the CP2102 USB-UART |
| Upload speed | not set | 460800 |

These build warnings are expected and harmless: narrowing conversions in `Adafruit_VC0706.cpp`, RadioLib's note about USB CDC debug output (V4-R8), and (with `audio = 1`) deprecated I2S format constants.

## 4. Connect the board

The V4-R8 has no USB-UART chip. It enumerates as the ESP32-S3's built-in USB Serial/JTAG device (VID:PID `303A:1001`) and shows up as `/dev/ttyACM0` on Linux, or a COM port on Windows.

The Heltec V2 has a CP2102 USB-UART instead. It shows up as `/dev/ttyUSB0` on Linux, or a COM port on Windows (which may need the CP210x driver; older macOS too). It resets into the bootloader automatically for flashing, so no PRG/RST button sequence is needed, and the port doesn't re-enumerate on reset.

Below, `/dev/ttyACM0` is the V4-R8 port; use `/dev/ttyUSB0` for a V2.

### Linux permissions

If you get `Permission denied` on `/dev/ttyACM0` (or `/dev/ttyUSB0` for a V2), either add yourself to the serial group (`sudo usermod -aG dialout $USER`, then log in again) or install the [PlatformIO udev rules](https://docs.platformio.org/en/latest/core/installation/udev-rules.html).

### WSL2

WSL2 can't see USB devices until you attach them with [usbipd-win](https://github.com/dorssel/usbipd-win). In an **administrator** PowerShell on Windows:

```powershell
usbipd list                          # find the BUSID of "USB JTAG/serial debug unit" (V4-R8) or "CP210x USB to UART Bridge" (V2)
usbipd bind --busid <BUSID>          # once per device
usbipd attach --wsl --busid <BUSID>  # after every plug-in / reboot
```

The board then appears in WSL as `/dev/ttyACM0` (V4-R8) or `/dev/ttyUSB0` (V2; attach it the same way).

When the ESP32-S3 resets into the bootloader during flashing, the USB device re-enumerates. If the upload fails right after "Connecting...", run `usbipd attach --wsl --busid <BUSID> --auto-attach` in a separate window and retry. The V2's CP2102 doesn't re-enumerate on reset, so a one-time attach is enough.

## 5. Flash

Use one board per role. Label them.

```sh
pio run -e remote -t upload          # board that goes in the field (V2: -e remote_v2)
pio run -e local  -t upload          # board near Wi-Fi (V2: -e local_v2)
```

With both boards plugged in at the same time, pick the port explicitly:

```sh
pio device list
pio run -e local -t upload --upload-port /dev/ttyACM1
```

### If the upload can't connect

On a V2 the CP2102 normally resets the board into download mode by itself; if it doesn't, hold **PRG**, press and release **RST**, release **PRG**, and upload again.

On a V4-R8, put the board in download mode by hand. A remote node sleeps with its USB port gone (in both `motion` modes), so it always needs this:

1. Hold **PRG**.
2. Press and release **RST**.
3. Release **PRG**.
4. Run the upload again.
5. Press **RST** afterwards to start the new firmware.

## 6. Monitor

```sh
pio device monitor                      # 115200 baud; the baud rate is ignored by native USB (V4-R8)
pio device monitor -p /dev/ttyACM1      # V2: -p /dev/ttyUSB0
```

Native USB (V4-R8) re-enumerates on reset, so the monitor may lose output from the first moments after boot. Press RST with the monitor open, or reconnect it, to see the boot messages. The remote node goes to deep sleep after each picture and comes back when the PIR triggers (`motion = 1`) or its timer fires (`motion = 0`); on a V4-R8 the monitor reconnects each time (under WSL2, attach with `--auto-attach`). A V2's CP2102 keeps its port while the board sleeps, so the monitor stays connected.

The next step is [usage.md](usage.md).

## Bench test with two Heltec V2 boards

Two V2s make a test pair: one flashed as the remote node, one as the base station. Every V2 has an OLED, and the V2 environments draw status on it by default (`display_v2 = 1`), so the boards can be followed without a serial monitor. A pair without a camera, using `test_image = 1` and `motion = 0`, has run this on the bench: the remote pushed the 91-packet test picture right after power-on, slept 60 s, woke on its timer and pushed again; both pictures were uploaded intact.

1. **USB driver.** The V2 connects through a CP2102 USB-UART. On Windows (and older macOS) install Silicon Labs' CP210x driver first; Windows may need a reboot to finish the install. Under WSL2, attach the port with `usbipd` as in [WSL2](#wsl2). On Linux it appears as `/dev/ttyUSB0` (the second board as `/dev/ttyUSB1`).
2. **Configure for the bench.** Set `motion` in `[loracamera]` on the remote only: with `motion = 0` (no PIR) the remote pushes a picture right after power-on and then every `poll_interval` seconds, so set a short `poll_interval`, such as `60`, for the bench (the default is 3600). With the default `motion = 1` it deep-sleeps until its PIR fires; with no PIR, jumper GPIO32 to 3V3 for about 1 s to simulate motion (not yet run on hardware). The base station ignores all of these. `sdcard = 1` is fine without a card (the base station logs `SD card not found` and carries on); set `sdcard = 0` to skip it. Create `src/local/auth.h` ([Wi-Fi credentials](#wi-fi-credentials-required)); without a reachable network the base station still runs, it just skips uploads.
3. **Flash**, with both boards plugged in, picking each port explicitly:
   ```sh
   pio run -e remote_v2 -t upload --upload-port /dev/ttyUSB1
   pio run -e local_v2  -t upload --upload-port /dev/ttyUSB0
   ```
   Which board is on which port depends on the attach order; `esptool.py --port /dev/ttyUSB0 read_mac` tells them apart.
4. **Expected screens.**
   - Remote: `- Remote Station -`, `Ready`, `LoRa OK`, then `Wakes every N s` (`motion = 0`) or `Sleeps until motion` (`motion = 1`). With `motion = 0` it then shows `Taking picture...` and `Announcing`, `picture`, `N packets`; with `motion = 1` that follows `Motion detected` when the PIR triggers. After the transfer it shows `Transfer done`.
   - Base station: `- Base Station -`, `Ready`, the Wi-Fi IP or the Wi-Fi problem (`Wi-Fi: SSID not found` / `Wi-Fi: not connected`), the SD card state, `Listening for remote`. When the remote announces a picture it shows `Receiving` with the packet progress and RSSI, then `Received`.
   - With `motion = 0` the base station logs `Picture announced, N packets` right after the remote powers on (boot, then the capture, about half a minute with a camera), and again every `poll_interval` seconds plus the capture and transfer time.
   - The top line names the role the board was flashed with. The two V2s are identical, so if the boards seem swapped, trust the top line (or `esptool.py read_mac` on each port), not their position on the desk.
   - `Starting LoRa failed!` on either board means its radio didn't start: see [troubleshooting.md](troubleshooting.md).
5. **Without a camera**, build the remote with `test_image = 1` ([configuration.md](configuration.md#test-image-no-camera)) and reflash the remote only: it pushes a built-in 22704-byte test JPEG in place of each capture, so a full transfer runs end to end. You'll see packet progress and RSSI on both screens, then the base station saves the picture (`sdcard = 1`) and uploads it. With `test_image = 0` and no camera, the remote shows `No picture`, logs `Nothing to send` and announces nothing, so nothing crosses the link.
6. **Keep the boards a few metres apart.** Side by side the signal is so strong (RSSI around −27 dBm) that some packets fail their CRC (`LoRa receive error: -7`); each failure costs a 4 s retry, which roughly doubles the transfer time.

All screens are listed in [configuration.md](configuration.md#oled-display).
