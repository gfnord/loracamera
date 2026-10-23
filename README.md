# loracamera

transmit images (and optionally sound) over a 915MHz LoRa link to a base station that saves them to a microSD card and uploads them over wifi. Set up to send a frame from a serial TTL camera when a PIR motion sensor sees movement, or on a timer every `poll_interval` seconds without the PIR (the field board deep-sleeps the rest of the time and always starts the transfer; the base station just listens), re-encoded on the field board to cut LoRa airtime (grayscale at night), plus a few seconds of microphone audio when audio is enabled in `platformio.ini` (timer mode, without the PIR). It can be adapted for most data you want to send using LoRa directly. 

Hardware: two [Heltec WiFi LoRa 32 V4-R8](https://heltec.org/project/wifi-lora-32-v4/) boards (ESP32-S3R8 + SX1262, with or without the OLED), each with a LiPo and a solar panel; the [Adafruit 613 weatherproof serial camera with IR LEDs](https://www.adafruit.com/product/613) (VC0706); a 3.3 V PIR motion sensor such as the AM312 on the field board; a 3.3 V microSD breakout on the base station; and, for audio only, an [Adafruit I2S MEMS microphone](https://www.adafruit.com/product/3421). The original build (Heltec V2) reached about 1km in a dense wooded place. The Heltec WiFi LoRa 32 V2 (ESP32 + SX1276, no PSRAM) is supported again as an alternative for either node, and the two boards can be mixed (`remote_v2`, `local_v2`). A pair of V2s has run the LoRa link, OLED, Wi-Fi upload and timer wake-ups on the bench with the built-in test picture; the camera, PIR, SD card and audio on the V2, and mixed pairs, are untested. A V2 remote sends the camera JPEG without re-encoding.

Supported cameras: the Adafruit 613 (VC0706, default) and a Sipeed Maix Nano (K210, MaixPy) running [`camera/maix/main.py`](camera/maix/main.py), which emulates the VC0706 (`camera_type = maix`, no night vision, untested on hardware: [docs/maix.md](docs/maix.md)).

## Wiring

**Remote node (field):**

![Wiring diagram: Heltec V4-R8 remote node with Adafruit 613 camera, 5 V boost converter, PIR motion sensor, LiPo, solar panel and optional I2S microphone](docs/images/wiring.svg)

**Base station (local node):**

![Wiring diagram: Heltec V4-R8 base station with microSD breakout, LiPo, solar panel, LoRa and Wi-Fi antennas](docs/images/wiring-base.svg)

The diagrams show the V4-R8. Wiring for the Heltec V2 (different GPIOs): [docs/hardware.md](docs/hardware.md#heltec-v2).

Pin details, bill of materials, power budget and camera specs: [docs/hardware.md](docs/hardware.md).

## Quick start

```sh
uv tool install platformio                   # or: pipx install platformio
printf '#define WIFI_SSID "ssid"\n#define WIFI_PASS "pass"\n#define SERVER_URL "http://192.168.0.150:8000"\n#define UPLOAD_TOKEN "change-me-long-secret"\n' > src/local/auth.h
# options live in [loracamera] in platformio.ini
pio run -e remote -t upload                  # field board
pio run -e local  -t upload                  # wifi-side board
# Heltec V2 boards instead: -e remote_v2 / -e local_v2 (any mix of the two board types works)
docker run -d -p 8000:8000 -e UPLOAD_TOKEN=change-me-long-secret -v "$PWD/captures:/captures" gfnord/loracamera-server
# upload server (docs/usage.md); captures/ must be writable by UID 10001
```

## Documentation

Full documentation is in [`docs/`](docs/README.md):

- [Hardware](docs/hardware.md): bill of materials, pin map, wiring of both nodes, power and solar sizing
- [Getting started](docs/getting-started.md): install, configure, build, flash, monitor (incl. WSL2)
- [Configuration](docs/configuration.md): `[loracamera]` options (motion detection, audio, OLED, SD card, camera speed, image re-encoding), radio, TX power, timing, pins
- [Usage](docs/usage.md): upload server, deployment, output files
- [Protocol](docs/protocol.md): LoRa packet formats and throughput
- [Troubleshooting](docs/troubleshooting.md)
