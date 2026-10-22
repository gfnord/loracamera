# LoRa transfer protocol

All packets are raw LoRa frames: explicit header, 2-byte CRC, sync word `0x12`. The radio settings are in [configuration.md](configuration.md#radio-link). Multi-byte numbers are **little-endian** `uint16`, packed by `u0()`/`u1()` and unpacked by `u()` in `lib/Board/Board.h`. The Heltec V4-R8 (SX1262) and the Heltec V2 (SX1276) use the same on-air format and sync word, so nodes on either board interoperate. Mixed pairs have not been tested on hardware.

## Packet types

| Name | Direction | Length | Bytes |
|---|---|---|---|
| Request for transmission (RFT) | local → remote | 2 | `F9 FC` = picture, `F9 FD` = audio (only sent and answered when built with `audio = 1`). `motion = 0` only |
| RFT response / announcement | remote → local | 4 | `F9 C2 nL nH`, where `n` = number of data packets. With `motion = 1` the remote sends it unprompted after a PIR-triggered capture |
| Packet request | local → remote | 4 | `F9 FE iL iH`, where `i` = packet index, starting at 0 |
| Data packet | remote → local | 3–252 | `iL iH` + up to 250 payload bytes |
| Done | local → remote | 2 | `F9 FA`: transfer finished, the remote can sleep. `motion = 1` only |

The remote node tells packet types apart by **length** (2 or 4 bytes) and then checks the leading bytes. Anything else is ignored. Both nodes must be built with the same `motion` value: a polling local node never gets an answer from a motion remote, and a motion local node never asks a polling remote.

Every data packet carries `LORA_TRANSFER_BUFFER` (250) payload bytes. The last one carries `content_length % 250` bytes, unless the content is an exact multiple of 250. `n = ceil(content_length / 250)`.

## Polling mode (motion = 0)

```mermaid
sequenceDiagram
    participant L as Local (Wi-Fi side)
    participant R as Remote (field)
    participant S as listen.py
    Note over L: every CHECK_EVERY_MS (10 s)
    L->>R: RFT F9 FC (picture) / F9 FD (audio, if enabled)
    Note over R: capture, power the camera off,<br/>re-encode into transfer_buffer<br/>(picture ≈ 25 s, audio 5 s)
    R->>L: F9 C2 n (sent 5 times, 20 ms apart)
    Note over L: wait out the repeats (~180 ms at SF7)
    loop i = 0 .. n-1
        L->>R: F9 FE i
        R->>L: i + payload[i*250 ..] (after a 5 ms turnaround guard)
        Note over L: append to transfer_buffer,<br/>log i / n and RSSI (and on the OLED with the display on)
    end
    L->>S: HTTP PUT /image.jpg or /audio.raw
    Note over L: audio = 1: switch picture ↔ audio
```

## Motion mode (motion = 1)

The remote node starts the transfer, then lets the local node pull it exactly as above.

```mermaid
sequenceDiagram
    participant L as Local (Wi-Fi side)
    participant R as Remote (field)
    participant S as listen.py
    Note over R: deep sleep until the PIR output goes HIGH
    Note over R: wake, capture, power the camera off,<br/>re-encode into transfer_buffer (≈ 25 s)
    loop until the first packet request, at most 6 times, 5 s apart
        R->>L: F9 C2 n (sent 5 times, 20 ms apart)
    end
    Note over L: wait out the repeats (~180 ms at SF7)
    loop i = 0 .. n-1
        L->>R: F9 FE i
        R->>L: i + payload[i*250 ..]
    end
    L->>R: F9 FA (done, sent 3 times)
    Note over R: deep sleep (wakes again at once if the PIR is still HIGH)
    L->>S: HTTP PUT /image.jpg
```

The local node never transmits on its own in this mode: it only listens for `F9 C2 n`, so it receives pictures whenever it's up, including during the first `WIFI_CONNECT_TIMEOUT_MS` after boot thanks to the repeated announcements. "Done" goes out before the SD write and upload, so the remote sleeps as early as possible.

## Timeouts and retries

On the local node:

| Phase | Wait | Retries | Worst case |
|---|---|---|---|
| RFT → RFT response (`motion = 0`) | `MAX_RFT_RESPONSE_MS` = 30 s | `REQUEST_TIMER` = 200 | ~100 min, then `Request timer timed out`. The next RFT goes out right away (the 10 s interval is counted from the start of the previous attempt) |
| Packet request → data packet (`motion = 0`) | `RESPONSE_TIMER` × ~20 ms ≈ 4 s | `REQUEST_TIMER` = 200, resent right after each timeout | ~14 min per packet. The packet is then **skipped**, and the uploaded file is shorter and corrupted |
| Packet request → data packet (`motion = 1`) | ≈ 4 s per attempt | until `MOTION_PACKET_GIVE_UP_MS` = 20 s | 20 s. The rest of the transfer is then **abandoned** (`Remote stopped answering`), and the file is saved and uploaded truncated |

On the remote node with `motion = 1`:

| Phase | Limit | Then |
|---|---|---|
| Announcement → first packet request | `MOTION_ANNOUNCE_TRIES` × `MOTION_ANNOUNCE_GAP_MS` = 6 × 5 s | `The local node never answered; picture dropped`, back to sleep |
| Between packet requests | `MOTION_SERVE_IDLE_MS` = 30 s | `No request for 30 s, stopping`, back to sleep. Only happens when all three "done" packets are lost, or the local node gave up |

Filtering on the local node while waiting for data:

- 4-byte frames starting with `F9`, i.e. the repeated RFT responses, are discarded as "magic packet".
- Frames whose index doesn't match the requested one are discarded as "Mismatched packet". The same request is resent on the next attempt.
- Frames shorter than 2 bytes or longer than 252, and frames that would overflow the receive buffer (`MAX_TRANSFER_BUFFER`: 125000 bytes on the V4-R8, 72000 on the V2), are discarded as "Malformed packet".
- Frames with a bad CRC are dropped by the radio layer (`LoRa receive error: -7`).

The remote node is stateless between requests. It serves any packet index from its last capture, so a lost packet only costs a re-request. Requests for an index past the end of the capture are ignored.

**Announcement larger than the local buffer.** When the local node receives the RFT response or announcement, it checks `n × 250` against its own `MAX_TRANSFER_BUFFER`. If the picture doesn't fit (for example a V4-R8 remote, 125000 bytes, talking to a V2 local node, 72000 bytes), it logs `N packets don't fit this board's M-byte buffer; skipped` and requests no packets. With `motion = 1` it still sends "done" (`F9 FA`), so the remote goes to sleep.

## Edge cases

- **Capture failed:** if the camera can't take a picture, `content_length` is 0. With `motion = 0` the remote answers `F9 C2 00 00`; the local node treats 0 packets as "nothing to do" and asks for a picture again after `CHECK_EVERY_MS`, and with `audio = 1` it doesn't switch to audio in that case. With `motion = 1` the remote announces nothing (`No picture to send`) and goes back to sleep.
- **Motion during a transfer (`motion = 1`):** the remote is awake and busy, so the PIR isn't watched. If its output is still HIGH when the remote goes back to sleep, the next picture follows at once; a shorter movement in between is missed.
- **Audio request to a picture-only remote:** a remote node built with `audio = 0` ignores `F9 FD` entirely. Build both nodes with the same `audio` value.
- **Remote busy:** the remote handles one request at a time and doesn't listen while it captures (up to ~25 s for a picture). The local node waits up to `MAX_RFT_RESPONSE_MS` for the answer.
- **Short last packet:** if the last packet has only 1–2 payload bytes (total length < 5) and the low byte of its index is `0xF9`, the local node mistakes it for an RFT response and discards it. That only happens when `content_length % 250` is 1 or 2 and `n − 1 ≡ 249 (mod 256)`. It's rare, and it costs the last 1–2 bytes.
- **Upload:** a failed PUT (no Wi-Fi, server down, non-2xx status) is logged but not retried. With `sdcard = 1` the file was already saved to the SD card first, so it isn't lost.

## Throughput

One data packet takes:

| Step | Time at SF7 / 250 kHz |
|---|---|
| Request airtime (4 bytes) | 15 ms |
| Remote poll interval (`delay(20)` in `loop()`), average | ~10 ms |
| Remote turnaround guard | 5 ms |
| Data airtime (252 bytes) | 197 ms |
| Local poll interval, average | ~10 ms |
| **Total** | **≈ 0.24 s → ≈ 1 KB/s** |

That puts a 48 KB camera JPEG at about 46 s, a 20 KB re-encoded one at about 19 s, and 5 s of audio (≈ 110 KB) at about 1.8 min. These are calculated estimates, not measurements. The data packet's airtime is now about 83% of the cycle. Further speed has to come from fewer bytes (re-encoding, resolution) or radio settings (bandwidth, SF).

Earlier versions waited a fixed 100 ms before every request and again before every reply (~0.45 s per packet). Those delays were inherited from the original Heltec V2 code and have been removed. `radioSend()` puts the radio back into receive mode as soon as each transmission ends. The remote's 5 ms guard covers that turnaround, and the one-time wait after the RFT response keeps the first request from colliding with the remote's repeated replies. The shorter timing hasn't been tested on hardware yet.
