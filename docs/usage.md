# Usage

## 1. Run the upload server

`server/` is `loracamera-server`, a small Rust HTTP server that saves the base station's uploads to disk. It's published as the Docker image `gfnord/loracamera-server` (linux/amd64 and linux/arm64, ~1 MB, runs as UID 10001).

```sh
mkdir -p ~/loracamera-captures && sudo chown 10001:10001 ~/loracamera-captures
docker run -d --name loracamera-server --restart unless-stopped -p 8000:8000 \
  -e UPLOAD_TOKEN=<secret> -v ~/loracamera-captures:/captures gfnord/loracamera-server
```

Without Docker: `cd server && UPLOAD_TOKEN=<secret> CAPTURE_DIR=~/loracamera-captures cargo run --release`.

Generate the token with `openssl rand -hex 32` and put the same value in `UPLOAD_TOKEN` in `src/local/auth.h` ([getting-started.md](getting-started.md#upload-server-address-and-token-required)).

| Variable | Default | Meaning |
|---|---|---|
| `UPLOAD_TOKEN` | required, ≥ 16 characters | Uploads must send `Authorization: Bearer <token>`; others get `401` |
| `LISTEN_ADDR` | `0.0.0.0:8000` | Listen address and port |
| `CAPTURE_DIR` | `.` (`/captures` in the image) | Where files are saved; created if missing |
| `MAX_UPLOAD_BYTES` | `262144` | Larger uploads get `413`. The largest file a base station sends is 125000 bytes (V4-R8) |

- It accepts only `PUT` (`405` otherwise): there is no file listing or download, so pictures are only reachable on the server's disk.
- Every upload is saved as `MMDDHHMM.<ext>`: month, day, hour and minute of arrival in **UTC**. The extension is the last three characters of the URL path (letters and digits only, else `400`), so `jpg` or `raw`. A second upload with the same name is saved as `MMDDHHMM-2.<ext>`, `-3` …, never overwriting. The name has no year.
- Each request logs one line: client address (from `X-Forwarded-For` behind a proxy), method, path, status and the saved file name or rejection reason.

Test it before involving the boards:

```sh
curl -X PUT -H "Authorization: Bearer <secret>" --upload-file some.jpg http://<server>:8000/image.jpg
# → Saved "10181432.jpg" (23456 bytes)
```

Then set `SERVER_URL` and `UPLOAD_TOKEN` in `src/local/auth.h` and flash the local node.

### Production deployment (Docker Hub + Caddy)

The production server runs the image behind the host's Caddy, at `http://loracam.nobrega.com.br`:

- `/root/loracamera/docker-compose.yml` is a copy of [`server/docker-compose.yml`](../server/docker-compose.yml): the container joins Caddy's `proxy-network` and publishes no port. `/root/loracamera/.env` (mode 600) holds `UPLOAD_TOKEN=…`; files land in `/root/loracamera/captures` (owned by 10001).
- The Caddyfile (`/root/caddy/Caddyfile`) serves the name over both schemes:
  ```
  http://loracam.nobrega.com.br, https://loracam.nobrega.com.br {
  	reverse_proxy loracamera-server:8000
  }
  ```
  The ESP32 firmware doesn't do TLS, so it uploads over plain HTTP; because the `http://` site is explicit, Caddy doesn't redirect it to HTTPS. HTTPS (Let's Encrypt, automatic) is there for browsers: `nobrega.com.br` sends HSTS with `includeSubDomains`, so a browser that has visited it refuses plain HTTP on every subdomain. A browser only ever sees `405 Only PUT is supported`, which confirms the server is reachable. Apply Caddyfile changes with `docker exec caddy caddy reload --config /etc/caddy/Caddyfile`; the Caddyfile is a single-file bind mount, so edit it in place (`cat new > Caddyfile`), not with tools that replace the file.
- DNS: an `A` record for `loracam.nobrega.com.br` → the server, **DNS only** (grey cloud) in Cloudflare. A proxied record breaks uploads when Cloudflare redirects HTTP to HTTPS.

Release a new version from the repository root, then update the server:

```sh
docker buildx build --platform linux/amd64,linux/arm64 \
  -t gfnord/loracamera-server:<version> -t gfnord/loracamera-server:latest --push server
ssh root@<server> 'cd /root/loracamera && docker compose pull && docker compose up -d'
```

The Dockerfile cross-compiles both platforms natively (static musl binaries, no QEMU). With the default `docker` buildx driver, create a multi-platform builder first: `docker buildx create --driver docker-container --use`.

### Running the server under WSL2

By default WSL2 sits behind NAT, so the ESP32 can't reach a server running inside WSL. Pick one of these:

- **Run it on Windows instead**, with Docker Desktop (same `docker run` as above), and use the Windows LAN IP in `SERVER_URL`. Allow inbound TCP 8000 through Windows Defender Firewall.
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
2. Power the **remote** node: battery + solar, or USB. With `motion = 1` (default) it initialises the radio, logs `Sleeping until the PIR on GPIO6 sees motion` (GPIO32 on a V2) and goes to deep sleep; on a V4-R8 its USB serial port disappears until the PIR triggers. With `motion = 0` it stays awake and waits for requests.
3. Power the **local** node (base station): battery + solar, or USB. It starts the radio (after resetting the OLED, with the display on), mounts the SD card (with `sdcard = 1`) and tries Wi-Fi for up to 20 s. With `motion = 1` it then listens for pictures; with `motion = 0` it sends its first request after `poll_interval` seconds (10 by default). Without Wi-Fi it still collects pictures to the SD card.

The order doesn't matter. With `motion = 1` the remote repeats each announcement for 30 s, which covers the local node's boot; with `motion = 0` the local node keeps retrying until the remote node answers.

To test motion detection, wave a hand in front of the PIR: the remote wakes, and the picture starts arriving at the local node about 25 s later (capture and re-encoding; a V2 remote doesn't re-encode, so its picture is larger than a re-encoded one and takes longer to transfer).

### What the nodes show

Status goes to the **serial log** (below). With the display on (`display = 1` on a V4-R8; `display_v2 = 1`, the default, on a V2) both nodes also draw it on the **OLED**: the boot state, then each step of a cycle. While a picture is moving, the local node shows

```
Receiving
49 / 50 [98.00%]
rssi: -87
```

That's the packet just received, the total packet count, and the RSSI of that packet in dBm; the remote shows `Sending`, the packet it just sent and the RSSI of the request. After the transfer the local node shows the size, the SD card result and the upload result. If the radio fails to start, the log and the OLED show `Starting LoRa failed!`. All screens are listed in [configuration.md](configuration.md#oled-display).

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

Without Wi-Fi the log shows `Wi-Fi not connected yet to "<SSID>": <reason> (status N); continuing, uploads wait for it` at boot and `Wi-Fi not connected, upload skipped` after each picture (see [troubleshooting](troubleshooting.md#wi-fi-not-connected-yet--wi-fi-not-connected-upload-skipped)).

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

On a V2 remote the last line names GPIO32, and there is no `Re-encoded …` line: it logs `No PSRAM on this board: jpeg_quality is ignored, camera JPEGs are sent unchanged` at boot and announces the camera JPEG's own size. The V2 build has not been tested on hardware; the log above is the V4-R8's.

With `motion = 0` the line before the packet requests is `RFT received. Content length is 12345; buffer count 250. Packets to send is 50`, there is no `Motion detected` / `Transfer done` / `Sleeping …`, and `audio = 1` builds also log `Recording 5 seconds of audio` and `microphone: 110592 bytes in 5.00s. ...`.

`reset with 1` means the camera answered and `reset with 0` means it didn't. The `Re-encoded` line shows the size the image was cut to, and whether it went out as grayscale (night) or colour, with the measured chroma (see [configuration.md](configuration.md#image-re-encoding)). With the display on, the remote's OLED shows the same steps.

With `motion = 1` a V4-R8's USB port re-enumerates on every wake-up, so the first lines may be lost before the monitor reconnects. `pio device monitor` reconnects by itself; under WSL2 attach the port with `usbipd attach --wsl --busid <BUSID> --auto-attach` so it follows the board ([getting-started.md](getting-started.md)). A V2 uses a CP2102 USB-UART, so its port doesn't disappear while the board sleeps.

## 3. Use the output files

### Pictures

Each picture exists in up to two places:

- **On the base station's SD card** (`sdcard = 1`): `/loracam/000001.jpg`, `000002.jpg` … numbered in arrival order and never overwritten. File dates are correct once Wi-Fi has set the clock (UTC). To read them, power the base station off, take the card out and copy the folder on any computer.
- **On the server** (when Wi-Fi is up): `MMDDHHMM.jpg` (UTC) in the server's `CAPTURE_DIR`; on the production server, `/root/loracamera/captures`.

Both are the same 640×480 JPEG (by default). With re-encoding on (V4-R8 remote) it's the remote node's re-encoded copy: grayscale for night shots, colour otherwise (see [configuration.md](configuration.md#image-re-encoding)). A V2 remote sends the camera's JPEG unchanged (`camera_compression` sets its size). Open it with any image viewer.

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

- **Range.** The original V2 build reached ~1 km through dense woods at ~20 dBm with SF7/250 kHz. The V2 firmware now transmits at 17 dBm (the SX1276's maximum continuous power), about 3 dB less than a V4-R8 at its default; range with the new V2 build hasn't been measured. To trade speed for range, raise the spreading factor first (see [configuration.md](configuration.md#range-vs-speed)), then TX power.
- **Antennas.** Height and line of sight matter more than power. Keep both antennas vertical and away from metal and the battery.
- **Battery life.** The numbers in this item are for the V4-R8; V2 deep-sleep current and per-picture cost have not been measured. With `motion = 1` (default) the remote node deep-sleeps between motion events (tens of µA [estimate]) and costs ~2–3 mAh per picture, so a small solar panel easily keeps up unless the PIR triggers all day. Aim the PIR away from moving branches, roads and sun-warmed surfaces. With `motion = 0` it never sleeps: about 75 mA in receive, and several hundred mA while transmitting or running the camera; with the default 10 s between pictures a solar-charged battery can't keep up, so raise `poll_interval` in `platformio.ini`. See [battery and solar sizing](hardware.md#battery-and-solar-sizing). The V2 has no solar input: a panel needs an external charge controller ([hardware.md](hardware.md#heltec-v2)).
- **Interference.** Other 915 MHz LoRa devices using sync word `0x12` (private networks) are received too. Packets that don't match the protocol are ignored. Changing `LORA_SYNC_WORD` on both nodes filters most of them out.
