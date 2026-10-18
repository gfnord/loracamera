# Configuration reference

All settings are compile-time constants. Change them, rebuild, and reflash. **Settings in `lib/HeltecV4R8/HeltecV4R8.h` are shared: reflash both boards after changing any of them.**

## Radio link (`lib/HeltecV4R8/HeltecV4R8.h`)

| Constant | Default | Meaning |
|---|---|---|
| `LORA_FREQ_MHZ` | `915.0` | Carrier frequency. The HTIT-WBR8H covers 863–928 MHz |
| `LORA_BW_KHZ` | `250.0` | LoRa bandwidth |
| `LORA_SF` | `7` | Spreading factor (7–12). Higher means more range and slower transfers |
| `LORA_CR` | `5` | Coding rate denominator, i.e. 4/5 |
| `LORA_SYNC_WORD` | `0x12` | Private-network sync word. Change it on both nodes to ignore other LoRa traffic |
| `LORA_PREAMBLE` | `8` | Preamble length in symbols |
| `LORA_TCXO_VOLTAGE` | `1.8` | Board-specific, don't change |
| `LORA_TX_POWER_DBM` | `7` | SX1262 output power (−9 to 22), see below |

RadioLib also enables a 2-byte payload CRC. Packets that fail it are dropped and requested again.

### Choosing the frequency

In Canada and the US use the 902–928 MHz ISM band. 915 MHz sits in the middle of it. Pick any value inside the band, set the same value on both nodes, and stay clear of the band edges with 250 kHz bandwidth.

This firmware transmits on a single fixed channel with no frequency hopping. Make sure that is acceptable for your use under ISED RSS-247 (Canada) or FCC Part 15.247 (US).

### TX power

`LORA_TX_POWER_DBM` sets the SX1262 output. The KCT8103L amplifier adds gain on top of it, and the gain shrinks as the input rises. Measured gain, from Meshtastic's KCT8103L table:

| `LORA_TX_POWER_DBM` | Front-end gain | ≈ Antenna power |
|---|---|---|
| 0 | +13 dB | 13 dBm |
| 7 (default) | +13 dB | 20 dBm (same as the original V2 build) |
| 13 | +13 dB | 26 dBm |
| 17 | +11 dB | 28 dBm |
| 21 | +7 dB | 28 dBm |

Above ~17 the output saturates near Heltec's 28 ± 1 dBm rating, while current draw keeps rising (~750 mA at 27 dBm). Raising SF usually buys more range per milliamp than raising power. Antenna gain adds to EIRP, which is the figure regulators limit.

### Range vs. speed

Airtime for one full data packet (252 bytes, explicit header, CRC on, CR 4/5). Transfer times also count the request packet and the fixed delays in the protocol (see [protocol.md](protocol.md#throughput)), assuming no retries:

| SF / BW | Data packet airtime | ≈ 48 KB JPEG (192 packets) | ≈ 5 s audio (442 packets) |
|---|---|---|---|
| SF7 / 250 kHz (default) | 0.20 s | 1.4 min | 3.3 min |
| SF9 / 250 kHz | 0.63 s | 3.0 min | 6.8 min |
| SF10 / 125 kHz | 2.3 s | 8.6 min | 20 min |

These are calculated with the Semtech airtime formula, not measured.

**Limit:** the local node waits `RESPONSE_TIMER` × 20 ms ≈ 4 s for each data packet. The remote node's 100 ms delay plus the data packet airtime must fit in that window. That rules out SF11 and SF12 at 125 kHz (≈ 4.5 s and 9 s per packet) unless you lengthen the window by raising the `delay(20)` inside the response loop of `handle_packets()`. `RESPONSE_TIMER` is a `uint8_t` and can't go above 255. RadioLib enables low-data-rate optimisation automatically where the SX1262 needs it (SF11/SF12 at 125 kHz).

## Transfer protocol (`lib/HeltecV4R8/HeltecV4R8.h`)

| Constant | Default | Meaning |
|---|---|---|
| `MAX_TRANSFER_BUFFER` | `125000` | Size of the capture/receive buffer on each node (bytes). Captures must fit |
| `LORA_TRANSFER_BUFFER` | `250` | Payload bytes per data packet. Each packet also carries a 2-byte header, and the LoRa maximum is 255 |

## Local node (`src/local/main.cpp`)

| Constant | Default | Meaning |
|---|---|---|
| `SERVER_URL` | `"http://192.168.0.150:8000"` | Upload target, without a trailing slash. Files are PUT to `/image.jpg` and `/audio.raw` |
| `WIFI_SSID`, `WIFI_PASS` | from `auth.h` | 2.4 GHz network |
| `CHECK_EVERY_MS` | `10000` | Pause after a finished transfer before the next request |
| `MAX_RFT_RESPONSE_MS` | `30000` | How long to wait for the remote node to capture and answer a request. Must exceed the remote's capture time: picture ≈ 2.5 s of fixed delays + the image read (≈ 20 s at 640×480, per the code comment) + 1 s power-off; audio = `AUDIO_SECONDS` |
| `REQUEST_TIMER` | `200` | Retries for a request, and per data packet. Must fit in a `uint8_t` (≤ 255) |
| `RESPONSE_TIMER` | `200` | Polls (20 ms each) waiting for each data packet, ≈ 4 s. Must be ≤ 255 |

The first request after boot is for a **picture**. After each completed transfer the type alternates between picture and audio.

## Remote node (`src/remote/main.cpp`)

### Pins

| Define | Default | Function |
|---|---|---|
| `CAMERATX_BOARDRX` | 47 | UART1 RX (camera TX) |
| `CAMERARX_BOARDTX` | 48 | UART1 TX (camera RX) |
| `CAMERA_ONOFF` | 41 | Camera power control, HIGH = on |
| `I2S_BCK` | 4 | Mic bit clock |
| `I2S_LRC` | 3 | Mic word select |
| `I2S_DIN` | 6 | Mic data |

Pick replacements only from the free pins listed in [hardware.md](hardware.md#v4-r8-pin-map): 3, 4, 6, 15, 16, 38, 39, 41, 42, 47, 48. Avoid 45 and 46, which are strapping pins, and 43/44, where the ROM bootloader prints at boot.

### Audio

| Constant | Default | Meaning |
|---|---|---|
| `SAMPLE_RATE` | `11025` | Hz |
| `AUDIO_SECONDS` | `5` | Recording length. `SAMPLE_RATE × AUDIO_SECONDS × 2` must stay below `MAX_TRANSFER_BUFFER` |
| `GAIN_FACTOR` | `2` | Each 32-bit I2S sample is shifted right by `12 − GAIN_FACTOR` before truncating to 16 bits. +1 doubles the volume |
| `AUDIO_FRAME_SIZE` | `1024` | I2S DMA buffer and read size, in bytes |

The output is mono signed 16-bit little-endian PCM (s16le). Default: 5 s × 11025 Hz × 2 B ≈ 110 KB.

### Camera

| Setting | Default | Where |
|---|---|---|
| Resolution | `VC0706_640x480` | `turnOnCamera()`; alternatives `VC0706_320x240`, `VC0706_160x120` |
| Serial speed | 38400 baud | `Serial1.begin(38400, …)` in `setup()`; the camera's default speed |
| Chunk size | 64 bytes | `takePicture()`; the camera buffer is 100 bytes |
| Response retries | `SEND_REPEAT` = 5 | The reply to a request is sent 5 times |

Smaller resolutions transfer much faster. See the [image size limits](troubleshooting.md#large-images-are-corrupted).

## Adding a new payload type

The transport moves any byte buffer. To send something else, such as sensor logs:

1. **Remote:** in `loop()`, accept a new second request byte next to `0xFC` (picture) and `0xFD` (audio), for example `0xFB`. `0xFE` and `0xC2` are already used, see [protocol.md](protocol.md). Fill `transfer_buffer` and set `content_length` (≤ `MAX_TRANSFER_BUFFER`). The existing code after the capture then announces the packet count and serves the packets.
2. **Local:** send the new byte from `request_transmission()`, which currently sends `0xFC + content_type`. Give it a URL and `Content-Type` in `put_to_server()`.
3. **Server:** `listen.py` takes the file extension from the last three characters of the URL path, so `/data.csv` is saved as `MMDDHHMM.csv`.

Packet formats are listed in [protocol.md](protocol.md).
