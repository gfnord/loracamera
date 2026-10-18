# LoRa transfer protocol

All packets are raw LoRa frames: explicit header, 2-byte CRC, sync word `0x12`. The radio settings are in [configuration.md](configuration.md#radio-link-libheltecv4r8heltecv4r8h). Multi-byte numbers are **little-endian** `uint16`, packed by `u0()`/`u1()` and unpacked by `u()` in `lib/HeltecV4R8/HeltecV4R8.h`.

## Packet types

| Name | Direction | Length | Bytes |
|---|---|---|---|
| Request for transmission (RFT) | local → remote | 2 | `F9 FC` = picture, `F9 FD` = audio (only sent and answered when built with `audio = 1`) |
| RFT response | remote → local | 4 | `F9 C2 nL nH`, where `n` = number of data packets |
| Packet request | local → remote | 4 | `F9 FE iL iH`, where `i` = packet index, starting at 0 |
| Data packet | remote → local | 3–252 | `iL iH` + up to 250 payload bytes |

The remote node tells packet types apart by **length** (2 or 4 bytes) and then checks the leading bytes. Anything else is ignored.

Every data packet carries `LORA_TRANSFER_BUFFER` (250) payload bytes. The last one carries `content_length % 250` bytes, unless the content is an exact multiple of 250. `n = ceil(content_length / 250)`.

## Sequence

```mermaid
sequenceDiagram
    participant L as Local (Wi-Fi side)
    participant R as Remote (field)
    participant S as listen.py
    Note over L: every CHECK_EVERY_MS (10 s)
    L->>R: RFT F9 FC (picture) / F9 FD (audio, if enabled)
    Note over R: capture into transfer_buffer<br/>(picture ≈ 25 s, audio 5 s)
    R->>L: F9 C2 n (sent 5 times, 20 ms apart)
    loop i = 0 .. n-1
        L->>R: F9 FE i
        R->>L: i + payload[i*250 ..]
        Note over L: append to transfer_buffer,<br/>OLED shows i / n and RSSI
    end
    L->>S: HTTP PUT /image.jpg or /audio.raw
    Note over L: audio = 1: switch picture ↔ audio
```

## Timeouts and retries (local node)

| Phase | Wait | Retries | Worst case |
|---|---|---|---|
| RFT → RFT response | `MAX_RFT_RESPONSE_MS` = 30 s | `REQUEST_TIMER` = 200 | ~100 min, then `Request timer timed out`. The next RFT goes out right away (the 10 s interval is counted from the start of the previous attempt) |
| Packet request → data packet | `RESPONSE_TIMER` × ~20 ms ≈ 4 s | `REQUEST_TIMER` = 200, with 100 ms between attempts | ~14 min per packet. The packet is then **skipped**, and the uploaded file is shorter and corrupted |

Filtering on the local node while waiting for data:

- 4-byte frames starting with `F9`, i.e. the repeated RFT responses, are discarded as "magic packet".
- Frames whose index doesn't match the requested one are discarded as "Mismatched packet". The same request is resent on the next attempt.
- Frames shorter than 2 bytes, or longer than 252, are discarded as "Malformed packet".
- Frames with a bad CRC are dropped by the radio layer (`LoRa receive error: -7`).

The remote node is stateless between requests. It serves any packet index from its last capture, so a lost packet only costs a re-request.

## Edge cases

- **Capture failed:** if the camera can't take a picture, `content_length` is 0 and the remote answers `F9 C2 00 00`. The local node treats 0 packets as "nothing to do" and asks for a picture again after `CHECK_EVERY_MS`. With `audio = 1` it doesn't switch to audio in that case.
- **Audio request to a picture-only remote:** a remote node built with `audio = 0` ignores `F9 FD` entirely. Build both nodes with the same `audio` value.
- **Remote busy:** the remote handles one request at a time and doesn't listen while it captures (up to ~25 s for a picture). The local node waits up to `MAX_RFT_RESPONSE_MS` for the answer.
- **Short last packet:** if the last packet has only 1–2 payload bytes (total length < 5) and the low byte of its index is `0xF9`, the local node mistakes it for an RFT response and discards it. That only happens when `content_length % 250` is 1 or 2 and `n − 1 ≡ 249 (mod 256)`. It's rare, and it costs the last 1–2 bytes.
- **Upload:** the HTTP status of the PUT is not checked. If the server is down, that capture is lost.

## Throughput

One data packet takes:

| Step | Time at SF7 / 250 kHz |
|---|---|
| Local delay before the request | 100 ms |
| Request airtime (4 bytes) | 15 ms |
| Local delay after transmit + remote poll | ~20–40 ms |
| Remote delay before the reply | 100 ms |
| Data airtime (252 bytes) | 197 ms |
| **Total** | **≈ 0.45 s → ≈ 550 B/s** |

That puts a 48 KB JPEG at about 1.4 min and 5 s of audio (≈ 110 KB) at about 3.3 min. These are calculated estimates, not measurements. The two fixed 100 ms delays are about 45% of the cycle at SF7. Shortening them (`delay(100)` in `handle_packets()` on the local node and before `radioSend()` in the remote's `loop()`) would speed transfers up, but only the original values have been tested over ~1 km.
