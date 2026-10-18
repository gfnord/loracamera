# Hardware

## Bill of materials

| Qty | Part | Notes |
|---|---|---|
| 2 | [Heltec WiFi LoRa 32 V4-R8](https://heltec.org/project/wifi-lora-32-v4/) (HTIT-WBR8H) | ESP32-S3R8 (8 MB PSRAM), 16 MB flash, SX1262, KCT8103L front end, 0.96" OLED. 868–928 MHz high-power model |
| 2 | 915 MHz antenna (IPEX) | Never transmit without an antenna attached |
| 1 | [Adafruit TTL serial JPEG camera (VC0706)](https://www.adafruit.com/product/613) | Remote node |
| 1 | [Adafruit I2S MEMS microphone (SPH0645)](https://www.adafruit.com/product/3421) | Remote node |
| 1 | Load switch / MOSFET for the camera supply | Driven by GPIO41 (see below) |
| 1–2 | LiPo cell (JST SH1.25 2-pin) and/or solar panel | Remote node power |

Heltec's V4-R8 datasheet (`WiFi_LoRa_32_V4R8.pdf`) and schematic (`HTIT-WBR8H_V4.3.2.pdf`) are the sources for this page. Heltec's copyright notice doesn't allow redistributing them, so they aren't committed. Get them from Heltec support or the product page.

## V4-R8 pin map

Pins used by the board itself. They're defined in `lib/HeltecV4R8/HeltecV4R8.h`.

| Function | GPIO | Notes |
|---|---|---|
| SX1262 NSS / SCK / MOSI / MISO | 8 / 9 / 10 / 11 | |
| SX1262 RESET / BUSY / DIO1 | 12 / 13 / 14 | DIO1 is the IRQ line; there is no DIO0 on the SX1262 |
| SX1262 TCXO | (SX1262 DIO3) | 32 MHz TCXO powered from DIO3, driven at 1.8 V |
| Front end power `VFEM_Ctrl` | 7 | Enables the FEM LDO (TLV75733); HIGH = on |
| Front end enable `PA_CSD` | 2 | HIGH = on |
| Front end mode `PA_CTX` | 5 | HIGH while transmitting, LOW for LNA receive |
| Front end `PA_CPS` | (SX1262 DIO2) | Switched by the radio (DIO2 as RF switch) |
| Vext control | 40 | P-MOSFET gate: **LOW = Vext on**. Vext feeds the OLED and the two `Ve` header pins |
| OLED SDA / SCL / RST | 17 / 18 / 21 | SSD1315, I2C address 0x3C, SSD1306-compatible |
| LED (white) | 46 | Also a strapping pin; unused by the firmware |
| PRG button | 0 | Hold at reset to enter the ROM bootloader |
| Battery voltage | 1 | 390K/100K divider, always connected; unused by the firmware |
| USB D− / D+ | 19 / 20 | Native USB; the board has **no** USB-UART bridge |
| GNSS connector | 38, 39, 41, 42 | Free when no GNSS module is plugged in |
| Not usable | 26, 33–37 | Used by the octal PSRAM inside the ESP32-S3R8 |

> **Datasheet errata.** Section 3.3 of the V4-R8 datasheet says to pull `VextCtrl(GPIO40)` high to enable Vext. The schematic shows it driving the gate of a P-channel MOSFET (Q2, AO3401A) pulled up to 3.3 V, so HIGH turns Vext **off**. The firmware drives it LOW. The datasheet's `ADC_CTRL(37)` footnote and the GPIO33–37 header entries were carried over from the 2 MB-PSRAM V4. On the R8 those pins are not connected.

### RF front end (KCT8103L)

From the KCT8103L truth table:

| Mode | CSD (GPIO2) | CTX (GPIO5) | CPS (DIO2) |
|---|---|---|---|
| Transmit (PA) | 1 | 1 | 1 |
| Receive (LNA) | 1 | 0 | X |
| Receive bypass | 1 | 1 | 0 |
| Shutdown | 0 | X | X |

`radioBegin()` powers the front end (GPIO7 HIGH, GPIO2 HIGH, GPIO5 LOW). `radioSend()` raises GPIO5 for the duration of each transmission, and the SX1262 drives CPS through DIO2 itself.

## Remote node wiring

All peripherals use free header GPIOs. The ESP32-S3 pins are **3.3 V only and not 5 V tolerant**.

| Peripheral pin | V4-R8 pin | Define in `src/remote/main.cpp` |
|---|---|---|
| Camera TX (out of camera) | GPIO47 (header J2) | `CAMERATX_BOARDRX` |
| Camera RX (into camera) | GPIO48 (header J2) | `CAMERARX_BOARDTX` |
| Camera power switch control | GPIO41 (header J3) | `CAMERA_ONOFF` |
| Mic BCLK | GPIO4 (header J3) | `I2S_BCK` |
| Mic LRCL / WS | GPIO3 (header J3) | `I2S_LRC` |
| Mic DOUT | GPIO6 (header J3) | `I2S_DIN` |
| Mic 3V / GND | 3V3 / GND | |
| Mic SEL | leave unconnected | see [troubleshooting](troubleshooting.md#audio-is-silent-or-noise) if audio is silent |

`CAMERA_ONOFF` is a logic signal (HIGH = camera on), not a supply. Use it to drive a load switch or a MOSFET that feeds the camera's supply. The firmware switches the camera on before each picture and off afterwards.

If the camera's serial lines run at 5 V, level-shift camera TX before GPIO47.

## Power

| Source | Notes |
|---|---|
| USB-C 5 V | Development; also charges the LiPo |
| LiPo, JST SH1.25 2-pin | 3.3–4.2 V; the remote node in the field |
| Solar, JST SH1.25 2-pin | 4.7–6 V; charges the LiPo |
| `Ve` pins | Switched 3.3 V output (Vext), up to 500 mA; on whenever the firmware runs |
| `3V3` pins | Always-on 3.3 V |

Current draw (datasheet, USB powered):

| State | Current |
|---|---|
| LoRa receive | ~75 mA |
| LoRa TX, 17 dBm | ~330 mA |
| LoRa TX, 22 dBm | ~400 mA |
| LoRa TX, 27 dBm | ~750 mA |
| Wi-Fi active | 120–170 mA |

The default TX setting gives about 20 dBm at the antenna (see [configuration.md](configuration.md#tx-power)). The remote node never sleeps: it stays in receive mode waiting for requests.
