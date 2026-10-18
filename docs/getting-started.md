# Getting started: configure, build, flash

The project is a [PlatformIO](https://platformio.org/) project using the Arduino framework. One `platformio.ini` defines two environments:

| Environment | Source | Board role |
|---|---|---|
| `local` | `src/local/` | Near Wi-Fi; receives the pictures (or requests them, with `motion = 0`) and uploads them |
| `remote` | `src/remote/` | In the field; PIR-triggered camera (+ microphone if audio is enabled, with `motion = 0`) |

Both are built for the same hardware (Heltec V4-R8) from the shared library `lib/HeltecV4R8`.

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
| `thingpulse/ESP8266 and ESP32 OLED driver for SSD1306 displays` | ^4.6.2 (local node; only linked with `display = 1`) |

If you'd rather use an IDE, the PlatformIO extension for VS Code opens this folder directly.

## 2. Configure

### Wi-Fi credentials (required)

Create `src/local/auth.h`. Git ignores it, so your password stays local:

```c
#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"
```

The `local` environment won't compile without this file. The ESP32-S3 only supports 2.4 GHz Wi-Fi.

### Upload server address (required)

Edit `SERVER_URL` in `src/local/main.cpp` and set it to the machine that runs `server/listen.py`:

```c
#define SERVER_URL "http://192.168.0.150:8000"
```

The port is fixed at 8000 by `listen.py`. If the server runs inside WSL2, see [usage.md](usage.md#running-the-server-under-wsl2).

### Motion detection (on by default)

The remote node sleeps and takes a picture only when a PIR sensor on GPIO6 sees movement. Wire the PIR as in [hardware.md](hardware.md#motion-sensor-pir). Without a PIR, set `motion = 0` in the `[loracamera]` section of `platformio.ini`: the local node then asks for a picture every 10 s and the remote stays awake. Flash **both** boards after changing it. See [configuration.md](configuration.md#motion-detection-pir).

### Audio capture (optional, off by default)

The firmware sends pictures only. To also capture microphone audio, set `audio = 1` and `motion = 0` in the `[loracamera]` section of `platformio.ini`, wire the microphone, and flash **both** boards. See [configuration.md](configuration.md#audio-capture-optional).

### Everything else

Radio settings, TX power, pins and timing have working defaults. Radio settings live in one shared header, so both nodes always match. The `[loracamera]` section of `platformio.ini` holds the project options:

| Option | Default | See |
|---|---|---|
| `motion` | `1` (PIR fitted) | [Motion detection](configuration.md#motion-detection-pir) |
| `audio` | `0` | [Audio capture](configuration.md#audio-capture-optional) |
| `display` | `0` (no OLED) | [OLED display](configuration.md#oled-display-optional) |
| `sdcard` | `1` (SD card on the base station) | [SD card](configuration.md#sd-card-base-station) |
| `camera_baud`, `camera_compression` | `115200`, `54` | [Camera](configuration.md#camera) |
| `jpeg_quality`, `night_grayscale`, `night_max_chroma` | `30`, `1`, `12` | [Image re-encoding](configuration.md#image-re-encoding) |

Everything else is described in [configuration.md](configuration.md).

## 3. Build

```sh
pio run                 # builds both environments
pio run -e local        # one environment
pio run -e remote
```

Output goes to `.pio/build/<env>/firmware.bin`. The build uses:

- `board = esp32-s3-devkitc-1`, with overrides for the V4-R8;
- 16 MB QIO flash with the `default_16MB.csv` partition table (6.25 MB app slot);
- `qio_opi` memory type for the 8 MB octal PSRAM;
- `ARDUINO_USB_CDC_ON_BOOT=1`, so `Serial` goes to the native USB port.

These build warnings are expected and harmless: narrowing conversions in `Adafruit_VC0706.cpp`, RadioLib's note about USB CDC debug output, and (with `audio = 1`) deprecated I2S format constants.

## 4. Connect the board

The V4-R8 has no USB-UART chip. It enumerates as the ESP32-S3's built-in USB Serial/JTAG device (VID:PID `303A:1001`) and shows up as `/dev/ttyACM0` on Linux, or a COM port on Windows.

### Linux permissions

If you get `Permission denied` on `/dev/ttyACM0`, either add yourself to the serial group (`sudo usermod -aG dialout $USER`, then log in again) or install the [PlatformIO udev rules](https://docs.platformio.org/en/latest/core/installation/udev-rules.html).

### WSL2

WSL2 can't see USB devices until you attach them with [usbipd-win](https://github.com/dorssel/usbipd-win). In an **administrator** PowerShell on Windows:

```powershell
usbipd list                          # find the BUSID of "USB JTAG/serial debug unit"
usbipd bind --busid <BUSID>          # once per device
usbipd attach --wsl --busid <BUSID>  # after every plug-in / reboot
```

The board then appears in WSL as `/dev/ttyACM0`.

When the ESP32-S3 resets into the bootloader during flashing, the USB device re-enumerates. If the upload fails right after "Connecting...", run `usbipd attach --wsl --busid <BUSID> --auto-attach` in a separate window and retry.

## 5. Flash

Use one board per role. Label them.

```sh
pio run -e remote -t upload          # board that goes in the field
pio run -e local  -t upload          # board near Wi-Fi
```

With both boards plugged in at the same time, pick the port explicitly:

```sh
pio device list
pio run -e local -t upload --upload-port /dev/ttyACM1
```

### If the upload can't connect

Put the board in download mode by hand. A remote node already running `motion = 1` firmware sleeps with its USB port gone, so it always needs this:

1. Hold **PRG**.
2. Press and release **RST**.
3. Release **PRG**.
4. Run the upload again.
5. Press **RST** afterwards to start the new firmware.

## 6. Monitor

```sh
pio device monitor                      # 115200 baud; the baud rate is ignored by native USB
pio device monitor -p /dev/ttyACM1
```

Native USB re-enumerates on reset, so the monitor may lose output from the first moments after boot. Press RST with the monitor open, or reconnect it, to see the boot messages. A remote node with `motion = 1` goes to sleep right after boot and comes back only when the PIR triggers; the monitor reconnects each time (under WSL2, attach with `--auto-attach`).

The next step is [usage.md](usage.md).
