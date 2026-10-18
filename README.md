# loracamera

transmit images (and optionally sound) over a 915MHz LoRa link to a base station that saves them to a microSD card and uploads them over wifi. Set up to send a frame from a serial TTL camera when a PIR motion sensor sees movement (the field board deep-sleeps the rest of the time), re-encoded on the field board to cut LoRa airtime (grayscale at night), plus a few seconds of microphone audio when audio is enabled in `platformio.ini` (polling mode, without the PIR). It can be adapted for most data you want to send using LoRa directly. 

Hardware: two [Heltec WiFi LoRa 32 V4-R8](https://heltec.org/project/wifi-lora-32-v4/) boards (ESP32-S3R8 + SX1262, with or without the OLED), each with a LiPo and a solar panel; the [Adafruit 613 weatherproof serial camera with IR LEDs](https://www.adafruit.com/product/613) (VC0706); a 3.3 V PIR motion sensor such as the AM312 on the field board; a 3.3 V microSD breakout on the base station; and, for audio only, an [Adafruit I2S MEMS microphone](https://www.adafruit.com/product/3421). The original build (Heltec V2) reached about 1km in a dense wooded place.

## Wiring

**Remote node (field):**

![Wiring diagram: Heltec V4-R8 remote node with Adafruit 613 camera, 5 V boost converter, PIR motion sensor, LiPo, solar panel and optional I2S microphone](docs/images/wiring.svg)

**Base station (local node):**

![Wiring diagram: Heltec V4-R8 base station with microSD breakout, LiPo, solar panel, LoRa and Wi-Fi antennas](docs/images/wiring-base.svg)

Pin details, bill of materials, power budget and camera specs: [docs/hardware.md](docs/hardware.md).

## Quick start

```sh
uv tool install platformio                   # or: pipx install platformio
printf '#define WIFI_SSID "ssid"\n#define WIFI_PASS "pass"\n' > src/local/auth.h
# set SERVER_URL in src/local/main.cpp to your PC's LAN IP; options live in [loracamera] in platformio.ini
pio run -e remote -t upload                  # field board
pio run -e local  -t upload                  # wifi-side board
python3 server/listen.py                     # saves uploads to the current directory
```

## Documentation

Full documentation is in [`docs/`](docs/README.md):

- [Hardware](docs/hardware.md): bill of materials, pin map, wiring of both nodes, power and solar sizing
- [Getting started](docs/getting-started.md): install, configure, build, flash, monitor (incl. WSL2)
- [Configuration](docs/configuration.md): `[loracamera]` options (motion detection, audio, OLED, SD card, camera speed, image re-encoding), radio, TX power, timing, pins
- [Usage](docs/usage.md): upload server, deployment, output files
- [Protocol](docs/protocol.md): LoRa packet formats and throughput
- [Troubleshooting](docs/troubleshooting.md)
