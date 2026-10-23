# Next steps (paused 2026-10-23)

Where work stopped, and what to pick up next. Last commit: `46f0ed6` (PIR cool-down), on top of tag `v0.2.0`.

## Board state

| Board | MAC | Port (WSL) | usbipd BUSID | Flashed with |
|---|---|---|---|---|
| Remote station | `24:0a:c4:fc:b3:e0` | `/dev/ttyUSB0` | 6-3 (COM6) | Test build: `motion = 1`, `test_image = 1`, `motion_cooldown = 20`. Asleep, waiting for the PIR on GPIO32 |
| Base station | `24:0a:c4:fc:b6:90` | `/dev/ttyUSB1` | 5-2 (COM8) | New protocol (always listens), built before the `0.2.0` version line: its boot screen doesn't show the version yet |

Ports follow the `usbipd attach` order and can swap: check with `pio pkg exec -p tool-esptoolpy -- esptool.py --port /dev/ttyUSBx read_mac`. After a reboot, reattach in PowerShell: `usbipd attach --wsl --busid <BUSID>` (`usbipd list` shows the BUSIDs).

Flash the remote with the test overrides without touching `platformio.ini`:

```sh
sed -e 's/^test_image = .*/test_image = 1/' -e 's/^motion_cooldown = .*/motion_cooldown = 20/' platformio.ini > /tmp/pio_test.ini
pio run -d . -c /tmp/pio_test.ini -e remote_v2 -t upload --upload-port /dev/ttyUSB0
pio run -e local_v2 -t upload --upload-port /dev/ttyUSB1
```

Opening a serial port resets the board (and restarts its timers).

## 1. Jumper test of the PIR cool-down (not yet run on hardware)

On the remote, connect GPIO32 to 3V3 for about 2 minutes, then remove it. Expected log with the 20 s test cool-down:

```
Motion detected -> Announcing picture ... -> Transfer done
Cool-down: PIR disarmed for 20 s
Cool-down over
Sleeping until the PIR on GPIO32 sees motion
Motion detected ...          (jumper still on: next picture at once)
```

With the jumper removed it must end on `Sleeping until the PIR ...` and stay asleep. Then reflash the remote with the real value (`motion_cooldown = 60` in `platformio.ini`).

## 2. Battery and sleep review (remote first)

### What the remote does now when it sleeps

| Part | In sleep |
|---|---|
| ESP32 / ESP32-S3 | Deep sleep |
| LoRa radio (SX1276 / SX1262) | Sleep mode, select line held HIGH (V4-R8: front end off, pins held) |
| Vext (OLED, `Ve` pins) | Off |
| Camera boost converter | EN held LOW |
| Wake-up, `motion = 0` | RTC timer only (`poll_interval`) |
| Wake-up, `motion = 1` | PIR on GPIO32 (V2) / GPIO6 (V4-R8); RTC peripherals kept on for the internal pull-down; after each picture a `motion_cooldown` timer sleep with the PIR disarmed |

### Gaps found

1. **Nothing is measured.** All power figures in `docs/hardware.md` are datasheet estimates; the V2's board-level sleep current is unknown. At 1 mA of sleep current the remote spends ~24 mAh/day asleep (as much as ~10 pictures); at 20 µA, ~0.5 mAh/day.
2. **Possible back-feeding into the unpowered OLED** [inference, to measure]: `gpio_deep_sleep_hold_en()` latches the OLED reset pin (GPIO16) HIGH and the I2C lines may keep pull-ups while Vext is off. Camera UART pins (GPIO17/23) float after `Serial1.end()`.
3. **Awake time per picture** (~2.3–3.7 mAh, estimate): CPU at 240 MHz, OLED powered while awake, a 1 s delay in `turnOffCamera()` just before sleeping, and a busy 20 ms polling loop while serving packets.
4. ~~No PIR cool-down~~: done (`motion_cooldown`, commit `46f0ed6`).
5. `motion = 1` keeps RTC peripherals on only for the internal pull-down; an external 100 kΩ pull-down (the AM312 output is push-pull) would allow powering them down. A few µA; low priority.

### Plan

| Phase | What | Status |
|---|---|---|
| 0. Measure | Multimeter in series with the LiPo, **USB unplugged** (USB powers the board's other parts). Remote sleep current with `motion = 0` and `motion = 1`, and the current through one capture cycle. Baseline for everything below | To do (meter available) |
| 1. Sleep leaks | Before sleeping: `Wire.end()`, drive OLED RST/SDA/SCL LOW, camera UART pins LOW, held. Re-measure | To do |
| 2. Awake time | (a) remote CPU at 80 MHz (raise for re-encoding on the V4-R8); (b) OLED off on a field remote; (c) drop the 1 s delay after the camera powers off; (d) maybe light sleep while waiting for packet requests | To do; decide (d) after Phase 0 |
| 3a. PIR cool-down | `motion_cooldown` | Done, needs the jumper test (step 1) |
| 3b. Low-battery guard | Skip captures below a threshold voltage; check the battery-divider pins on the V2 and V4-R8 schematics first | To do |

### Open question

- Should the OLED be off by default on the remote (field), and stay on for the base station?

## Housekeeping

- Firmware still reports `0.2.0` although `46f0ed6` adds a feature after tag `v0.2.0`: bump `LORACAMERA_VERSION` in `lib/Board/Board.h` (and tag) with the next release.
- `test_image = 0` in `platformio.ini`: without a camera the remote has nothing to send unless flashed with the test override above.
- Test pictures from the bench runs are on the server in `/root/loracamera/captures`; clear them with `ssh root@188.245.122.54 'rm /root/loracamera/captures/*'` when no longer needed.
- Viewing uploads: `rsync -av root@188.245.122.54:/root/loracamera/captures/ /mnt/c/Users/gusta/OneDrive/Pictures/loracam/` (a password-protected browser gallery was offered, not started).
