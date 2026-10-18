# Usage

## 1. Run the upload server

`server/listen.py` is Python's built-in HTTP server with a `PUT` handler added. It needs no dependencies.

```sh
mkdir -p ~/loracamera-captures && cd ~/loracamera-captures
python3 /path/to/loracamera/server/listen.py
```

- It listens on **port 8000, on all interfaces**. Python's `http.server.test()` sets the port and listen address; the script has no options.
- Every upload is saved to the **current directory** as `MMDDHHMM.<ext>` (month, day, hour, minute of arrival). The extension is the last three characters of the URL path, so `jpg` or `raw`.
- Two uploads of the same type within the same minute overwrite each other, and the name has no year.
- It also serves the current directory over `GET`, including a file listing, so anyone on the network can browse it. Run it only on a trusted LAN.

Test it from another machine before involving the boards:

```sh
curl -X PUT --upload-file some.jpg http://<server-ip>:8000/image.jpg
# → Saved "10181432.jpg"
```

Then set `SERVER_URL` in `src/local/main.cpp` to `http://<server-ip>:8000` and flash the local node.

### Running the server under WSL2

By default WSL2 sits behind NAT, so the ESP32 can't reach a server running inside WSL. Pick one of these:

- **Run it on Windows instead.** Run `py server\listen.py` from Windows Python and use the Windows LAN IP in `SERVER_URL`. Allow Python through Windows Defender Firewall when prompted.
- **Mirrored networking.** In `%UserProfile%\.wslconfig`:
  ```ini
  [wsl2]
  networkingMode=mirrored
  ```
  Then run `wsl --shutdown` and allow inbound TCP 8000 through the Hyper-V firewall (admin PowerShell):
  ```powershell
  New-NetFirewallHyperVRule -Name loracamera -DisplayName "loracamera 8000" -Direction Inbound -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -Protocol TCP -LocalPorts 8000
  ```
  The server is then reachable at the Windows host's LAN IP.

## 2. Bring up the nodes

1. Attach antennas to **both** boards before powering them.
2. Power the **remote** node: battery + solar, or USB. With `motion = 1` (default) it initialises the radio, logs `Sleeping until the PIR on GPIO6 sees motion` and goes to deep sleep; its USB serial port disappears until the PIR triggers. With `motion = 0` it stays awake and waits for requests.
3. Power the **local** node (base station): battery + solar, or USB. It starts the radio (after resetting the OLED, with `display = 1`), mounts the SD card (with `sdcard = 1`) and tries Wi-Fi for up to 20 s. With `motion = 1` it then listens for pictures; with `motion = 0` it sends its first request after ~10 s. Without Wi-Fi it still collects pictures to the SD card.

The order doesn't matter. With `motion = 1` the remote repeats each announcement for 30 s, which covers the local node's boot; with `motion = 0` the local node keeps retrying until the remote node answers.

To test motion detection, wave a hand in front of the PIR: the remote wakes, and the picture starts arriving at the local node about 25 s later (capture and re-encoding).

### What the local node shows

Progress goes to the **serial log** (below). Boards built with `display = 1` also draw it on the **OLED**, which stays blank until the first transfer and then shows:

```
49 / 50 [98.00%]
 rssi: -87
```

That's the packet just received, the total packet count, and the RSSI of that packet in dBm. If the radio fails to start, the log (and the OLED) shows `Starting LoRa failed!`.

**Serial log** of a normal picture cycle with `motion = 1` (`pio device monitor`; the numbers are examples):

```
SD card: 30436 MB free, next file 000001
Wi-Fi connected, IP 192.168.0.42
I have 123456 free heap
Motion picture announced, 50 packets
Got back 50 packets from the rft
0 / 50 [0.00%] rssi: -87
...
49 / 50 [98.00%] rssi: -86
Loaded all packets, total 12345
lora: 12345 bytes in 12.10s. 1020.25 bytes/s.
Saved /loracam/000001.jpg (12345 bytes)
server: 12345 bytes in 0.41s. ...
```

With `motion = 0` the first lines of each cycle are the request and its answer instead:

```
Sending request for transmission of type 0...
Receiving rft response, 50 packets
if packets! 50
```

Type 0 is a picture and type 1 is audio. With the default `audio = 0` every request is type 0. With `audio = 1` the types alternate after each completed transfer.

If the remote node stops answering in the middle of a transfer (`motion = 1`), the log shows `Remote stopped answering; abandoning the transfer at packet N` after 20 s, and the truncated picture is saved and uploaded.

Without Wi-Fi the log shows `Wi-Fi not connected yet; continuing, uploads wait for it` at boot and `Wi-Fi not connected, upload skipped` after each picture (see [troubleshooting](troubleshooting.md#wi-fi-not-connected-yet--wi-fi-not-connected-upload-skipped)).

### What the remote node logs

Example values, `motion = 1`:

```
I have 234567 free heap
Motion detected
Turning on camera; reset with 1
Camera serial at 115200 baud
Taking pic
len of frame is 48123
camera: 48123 bytes in 9.80s. ...
Turning off camera
Re-encoded 640x480 grayscale (chroma 3, quality 30): 48123 -> 12345 bytes in 0.90s
Announcing 12345 bytes in 50 packets
Packet 0 was requested. byte_start is 0. content_length 12345
...
Transfer done
Sleeping until the PIR on GPIO6 sees motion
```

With `motion = 0` the line before the packet requests is `RFT received. Content length is 12345; buffer count 250. Packets to send is 50`, there is no `Motion detected` / `Transfer done` / `Sleeping …`, and `audio = 1` builds also log `Recording 5 seconds of audio` and `microphone: 110592 bytes in 5.00s. ...`.

`reset with 1` means the camera answered and `reset with 0` means it didn't. The `Re-encoded` line shows the size the image was cut to, and whether it went out as grayscale (night) or colour, with the measured chroma (see [configuration.md](configuration.md#image-re-encoding)). The remote node has no display output.

With `motion = 1` the board's USB port re-enumerates on every wake-up, so the first lines may be lost before the monitor reconnects. `pio device monitor` reconnects by itself; under WSL2 attach the port with `usbipd attach --wsl --busid <BUSID> --auto-attach` so it follows the board ([getting-started.md](getting-started.md)).

## 3. Use the output files

### Pictures

Each picture exists in up to two places:

- **On the base station's SD card** (`sdcard = 1`): `/loracam/000001.jpg`, `000002.jpg` … numbered in arrival order and never overwritten. File dates are correct once Wi-Fi has set the clock (UTC). To read them, power the base station off, take the card out and copy the folder on any computer.
- **On the server** (when Wi-Fi is up): `MMDDHHMM.jpg` in the folder `listen.py` runs in.

Both are the same 640×480 JPEG (by default). With re-encoding on it's the remote node's re-encoded copy: grayscale for night shots, colour otherwise (see [configuration.md](configuration.md#image-re-encoding)). Open it with any image viewer.

### Audio

Only produced when both nodes are built with `audio = 1` (see [configuration.md](configuration.md#audio-capture-optional)).

`MMDDHHMM.raw` on the server, or `/loracam/NNNNNN.raw` on the base station's SD card, is headerless PCM: **signed 16-bit little-endian, mono, 11025 Hz** (with the defaults in [configuration.md](configuration.md#audio)). The HTTP `Content-Type` header says `audio/L8`, but the data is 16-bit. Convert or play it with:

```sh
ffmpeg -f s16le -ar 11025 -ac 1 -i 10181432.raw 10181432.wav
sox -t raw -r 11025 -e signed -b 16 -c 1 10181432.raw 10181432.wav
aplay -f S16_LE -r 11025 -c 1 10181432.raw
```

In Audacity use **File → Import → Raw Data** with *Signed 16-bit PCM*, *Little-endian*, *1 channel*, *11025 Hz*.

## 4. Field deployment tips

- **Range.** The original V2 build reached ~1 km through dense woods at ~20 dBm with SF7/250 kHz. To trade speed for range, raise the spreading factor first (see [configuration.md](configuration.md#range-vs-speed)), then TX power.
- **Antennas.** Height and line of sight matter more than power. Keep both antennas vertical and away from metal and the battery.
- **Battery life.** With `motion = 1` (default) the remote node deep-sleeps between motion events (tens of µA [estimate]) and costs ~2–3 mAh per picture, so a small solar panel easily keeps up unless the PIR triggers all day. Aim the PIR away from moving branches, roads and sun-warmed surfaces. With `motion = 0` it never sleeps: about 75 mA in receive, and several hundred mA while transmitting or running the camera; with the default 10 s between pictures a solar-charged battery can't keep up, so raise `CHECK_EVERY_MS`. See [battery and solar sizing](hardware.md#battery-and-solar-sizing).
- **Interference.** Other 915 MHz LoRa devices using sync word `0x12` (private networks) are received too. Packets that don't match the protocol are ignored. Changing `LORA_SYNC_WORD` on both nodes filters most of them out.
