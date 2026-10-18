# loracamera

transmit images (and optionally sound) over a 915MHz LoRa link to a base station near your wifi. Set up to send a frame from a serial TTL camera, re-encoded on the field board to cut LoRa airtime (grayscale at night), plus a few seconds of microphone audio when audio is enabled in `platformio.ini`. It can be adapted for most data you want to send using LoRa directly. 

Hardware: two [Heltec WiFi LoRa 32 V4-R8](https://heltec.org/project/wifi-lora-32-v4/) boards (ESP32-S3R8 + SX1262, with or without the OLED), the [Adafruit 613 weatherproof serial camera with IR LEDs](https://www.adafruit.com/product/613) (VC0706) and, for audio only, an [Adafruit I2S MEMS microphone](https://www.adafruit.com/product/3421). The original build (Heltec V2) reached about 1km in a dense wooded place.

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

- [Hardware](docs/hardware.md): pin map, RF front end, remote wiring, power
- [Getting started](docs/getting-started.md): install, configure, build, flash, monitor (incl. WSL2)
- [Configuration](docs/configuration.md): `[loracamera]` options (audio, OLED, camera speed, image re-encoding), radio, TX power, timing, pins
- [Usage](docs/usage.md): upload server, deployment, output files
- [Protocol](docs/protocol.md): LoRa packet formats and throughput
- [Troubleshooting](docs/troubleshooting.md)
