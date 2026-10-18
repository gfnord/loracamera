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
2. Power the **remote** node: battery, solar, or USB. It initialises the radio and waits for requests.
3. Power the **local** node within Wi-Fi range. It resets the OLED, starts the radio, joins Wi-Fi, then sends its first request after ~10 s.

The order doesn't matter. The local node keeps retrying until the remote node answers.

### What the local node shows

The **OLED** stays blank until the first transfer. During a transfer it shows:

```
57 / 193 [29.53%]
 rssi: -87
```

That's the packet just received, the total packet count, and the RSSI of that packet in dBm. If the radio fails to start, the OLED shows `Starting LoRa failed!`.

**Serial log** of a normal picture cycle (`pio device monitor`; the numbers are examples):

```
I have 123456 free heap
Sending request for transmission of type 0...
Receiving rft response, 193 packets
if packets! 193
Got back 193 packets from the rft
0 / 193 [0.00%] rssi: -87
...
192 / 193 [99.48%] rssi: -86
Loaded all packets, total 48123
lora: 48123 bytes in 87.12s. 552.38 bytes/s.
server: 48123 bytes in 0.41s. ...
```

Type 0 is a picture and type 1 is audio. They alternate after each completed transfer.

The local node prints nothing while it connects to Wi-Fi. If the log stops after boot, check `auth.h` (see [troubleshooting](troubleshooting.md#local-node-hangs-after-boot)).

### What the remote node logs

```
I have 234567 free heap
Turning on camera; reset with 1
Taking pic
len of frame is 48123
camera: 48123 bytes in 19.80s. ...
Turning off camera
RFT received. Content length is 48123; buffer count 250. Packets to send is 193
Packet 0 was requested. byte_start is 0. content_length 48123
...
Recording 5 seconds of audio
microphone: 110592 bytes in 5.00s. ...
```

`reset with 1` means the camera answered and `reset with 0` means it didn't. The remote node has no display output.

## 3. Use the output files

### Pictures

`MMDDHHMM.jpg` is a JPEG from the camera (640×480 by default). Open it with any image viewer.

### Audio

`MMDDHHMM.raw` is headerless PCM: **signed 16-bit little-endian, mono, 11025 Hz** (with the defaults in [configuration.md](configuration.md#audio)). The HTTP `Content-Type` header says `audio/L8`, but the data is 16-bit. Convert or play it with:

```sh
ffmpeg -f s16le -ar 11025 -ac 1 -i 10181432.raw 10181432.wav
sox -t raw -r 11025 -e signed -b 16 -c 1 10181432.raw 10181432.wav
aplay -f S16_LE -r 11025 -c 1 10181432.raw
```

In Audacity use **File → Import → Raw Data** with *Signed 16-bit PCM*, *Little-endian*, *1 channel*, *11025 Hz*.

## 4. Field deployment tips

- **Range.** The original V2 build reached ~1 km through dense woods at ~20 dBm with SF7/250 kHz. To trade speed for range, raise the spreading factor first (see [configuration.md](configuration.md#range-vs-speed)), then TX power.
- **Antennas.** Height and line of sight matter more than power. Keep both antennas vertical and away from metal and the battery.
- **Battery life.** The remote node never sleeps: about 75 mA in receive, and several hundred mA while transmitting or running the camera. Size the battery and solar panel for continuous operation.
- **Interference.** Other 915 MHz LoRa devices using sync word `0x12` (private networks) are received too. Packets that don't match the protocol are ignored. Changing `LORA_SYNC_WORD` on both nodes filters most of them out.
