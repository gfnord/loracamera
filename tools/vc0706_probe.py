#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""Take one picture from a VC0706 camera, or from a Maix running camera/maix/main.py,
through a USB-to-TTL adapter, with the command sequence and timeouts the remote firmware
uses (turnOnCamera() and takePicture() in src/remote/main.cpp, lib/VC0706).

    uv run tools/vc0706_probe.py /dev/ttyUSB0 --out probe.jpg

Wiring: adapter RX <- Maix IO6 (TX), adapter TX -> Maix IO8 (RX), GND <-> GND, 3.3 V logic.
Exit status 0 only if every reply was well-formed and the JPEG is complete.
"""

import argparse
import sys
import time

import serial

DEFAULT_BAUD = 38400
SIZES = {"640x480": 0x00, "320x240": 0x11, "160x120": 0x22}
SET_PORT_ARGS = {
    9600: [0x03, 0x01, 0xAE, 0xC8],
    19200: [0x03, 0x01, 0x56, 0xE4],
    38400: [0x03, 0x01, 0x2A, 0xF2],
    57600: [0x03, 0x01, 0x1C, 0x1C],
    115200: [0x03, 0x01, 0x0D, 0xA6],
}
CHUNK = 64  # bytes per read, as takePicture() in the remote firmware
READ_DELAY = 100  # CAMERADELAY in lib/VC0706
FBUF_MARK = bytes([0x76, 0x00, 0x32, 0x00, 0x00])


class ProbeError(Exception):
    pass


class Camera:
    """The parts of Adafruit_VC0706 the remote firmware uses, with the same timeouts."""

    def __init__(self, port):
        self.ser = serial.Serial(port, DEFAULT_BAUD, timeout=0)
        self.buf = b""

    def set_baud(self, baud):
        self.ser.baudrate = baud

    def send(self, cmd, args):
        self.ser.write(bytes([0x56, 0x00, cmd] + list(args)))

    def read_response(self, numbytes, timeout):
        # readResponse(): stop after numbytes, or after `timeout` 10 ms polls without data.
        buf = bytearray()
        idle = 0
        while idle != timeout and len(buf) != numbytes:
            data = self.ser.read(numbytes - len(buf))
            if not data:
                time.sleep(0.01)
                idle += 1
                continue
            idle = 0
            buf += data
        self.buf = bytes(buf)
        return self.buf

    def run_command(self, cmd, args, resplen, flush=True):
        if flush:
            self.read_response(100, 10)
        self.send(cmd, args)
        r = self.read_response(resplen, 200)
        if len(r) != resplen:
            raise ProbeError("command 0x%02x: %d of %d reply bytes: %s" % (cmd, len(r), resplen, r.hex(" ")))
        if r[0] != 0x76 or r[1] != 0x00 or r[2] != cmd or r[3] != 0x00:
            raise ProbeError("command 0x%02x: bad reply %s" % (cmd, r.hex(" ")))
        return r

    def get_version(self, timeout=200):
        self.send(0x11, [0x01])
        r = self.read_response(100, timeout)
        if len(r) > 5 and r[:4] == bytes([0x76, 0x00, 0x11, 0x00]):
            return r[5:].split(b"\x00")[0].decode("ascii", "replace").strip()
        return None

    def reset(self):
        self.run_command(0x26, [0x00], 5)

    def set_image_size(self, code):
        self.run_command(0x31, [0x05, 0x04, 0x01, 0x00, 0x19, code], 5)

    def set_compression(self, c):
        self.run_command(0x31, [0x05, 0x01, 0x01, 0x12, 0x04, c], 5)

    def read_register(self, args):
        return self.run_command(0x30, args, 6)[5]

    def set_port(self, baud):
        self.send(0x24, SET_PORT_ARGS[baud])
        r = self.read_response(100, 200)
        if len(r) < 5 or r[:4] != bytes([0x76, 0x00, 0x24, 0x00]):
            raise ProbeError("set port: bad reply %s" % r[:16].hex(" "))

    def fbuf_ctrl(self, op):
        self.run_command(0x36, [0x01, op], 5)

    def frame_length(self):
        r = self.run_command(0x34, [0x01, 0x00], 9)
        return int.from_bytes(r[5:9], "big")

    def read_picture(self, offset, n):
        args = [0x0C, 0x00, 0x0A] + list(offset.to_bytes(4, "big")) + [0, 0, 0, n, READ_DELAY >> 8, READ_DELAY & 0xFF]
        self.run_command(0x32, args, 5, flush=False)
        r = self.read_response(n + 5, READ_DELAY)
        if len(r) != n + 5:
            raise ProbeError("read at %d: %d of %d bytes" % (offset, len(r), n + 5))
        if r[n:] != FBUF_MARK:
            raise ProbeError("read at %d: bad trailer %s" % (offset, r[n:].hex(" ")))
        return r[:n]


class Timer:
    def __init__(self):
        self.start = time.monotonic()
        self.last = self.start
        self.rows = []

    def step(self, name, detail=""):
        now = time.monotonic()
        self.rows.append((name, (now - self.last) * 1000, detail))
        print("%-28s %8.0f ms  %s" % (name, (now - self.last) * 1000, detail), flush=True)
        self.last = now


def wait_for_camera(cam, target_baud, wait_s):
    # waitForMaix() in the remote firmware: get-version every 250 ms, every other one at
    # the readout speed, until the first answer.
    start = time.monotonic()
    poll = 0
    while time.monotonic() - start < wait_s:
        baud = target_baud if poll % 2 else DEFAULT_BAUD
        poll += 1
        sent = time.monotonic()
        cam.set_baud(baud)
        text = cam.get_version(25)
        if text is not None:
            cam.set_baud(DEFAULT_BAUD)
            return text, baud
        spent = time.monotonic() - sent
        if spent < 0.25:
            time.sleep(0.25 - spent)
    raise ProbeError("no answer to get-version within %.0f s" % wait_s)


def reset_camera(cam, target_baud):
    # resetCamera(): at 38400, then at the readout speed (camera power not cut).
    cam.set_baud(DEFAULT_BAUD)
    try:
        cam.reset()
        return DEFAULT_BAUD
    except ProbeError:
        if target_baud == DEFAULT_BAUD:
            raise
    cam.set_baud(target_baud)
    cam.reset()
    cam.set_baud(DEFAULT_BAUD)
    return target_baud


def probe(args):
    cam = Camera(args.port)
    t = Timer()
    if args.wait > 0:
        text, baud = wait_for_camera(cam, args.baud, args.wait)
        t.step("first answer", "%s (at %d baud)" % (text, baud))
    answered_at = reset_camera(cam, args.baud)
    t.step("reset", "at %d baud" % answered_at)
    time.sleep(0.1)
    text = cam.get_version()
    if text is None:
        raise ProbeError("no answer to get-version after the reset")
    t.step("get version", text)
    time.sleep(0.1)
    cam.set_image_size(SIZES[args.size])
    t.step("set image size", args.size)
    time.sleep(0.1)
    cam.set_compression(args.compression)
    t.step("set compression", str(args.compression))
    time.sleep(0.1)
    if args.baud != DEFAULT_BAUD:
        cam.set_port(args.baud)
        time.sleep(0.1)
        cam.set_baud(args.baud)
        if cam.get_version() is None:
            raise ProbeError("no answer at %d baud" % args.baud)
        t.step("set baud + get version", "%d baud" % args.baud)
    cam.fbuf_ctrl(0x03)
    time.sleep(1.0)
    t.step("resume + 1 s", "")
    cam.fbuf_ctrl(0x00)
    t.step("freeze", "")
    time.sleep(0.1)
    length = cam.frame_length()
    t.step("frame length", "%d bytes" % length)
    if length == 0:
        raise ProbeError("frame length 0: no picture")
    time.sleep(0.1)
    jpeg = bytearray()
    read_start = time.monotonic()
    while len(jpeg) < length:
        jpeg += cam.read_picture(len(jpeg), min(CHUNK, length - len(jpeg)))
    readout = time.monotonic() - read_start
    t.step("read %d-byte chunks" % CHUNK, "%d bytes, %.0f bytes/s" % (len(jpeg), len(jpeg) / readout))
    cam.fbuf_ctrl(0x03)
    t.step("resume", "")
    size = cam.read_register([0x04, 0x04, 0x01, 0x00, 0x19])
    compression = cam.read_register([0x04, 0x01, 0x01, 0x12, 0x04])
    if compression != args.compression:
        raise ProbeError("read-back: compression %d, expected %d" % (compression, args.compression))
    note = "" if size == SIZES[args.size] else " (not %s: framesize forced in maix_config.py?)" % args.size
    t.step("read back registers", "size 0x%02x, compression %d%s" % (size, compression, note))
    with open(args.out, "wb") as f:
        f.write(jpeg)
    if jpeg[:2] != b"\xff\xd8" or jpeg[-2:] != b"\xff\xd9":
        raise ProbeError("%s is not a complete JPEG (no SOI/EOI marker)" % args.out)
    print("saved %s: %d bytes, %.1f s in total" % (args.out, len(jpeg), time.monotonic() - t.start))


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("port", help="serial port of the USB-to-TTL adapter, e.g. /dev/ttyUSB0 or COM5")
    p.add_argument("--out", default="probe.jpg", help="JPEG file to write (default probe.jpg)")
    p.add_argument("--size", choices=sorted(SIZES), default="640x480", help="image size (default 640x480)")
    p.add_argument("--compression", type=int, default=54, help="compression register 0-255 (default 54)")
    p.add_argument("--baud", type=int, choices=sorted(SET_PORT_ARGS), default=115200,
                   help="readout speed, camera_baud (default 115200)")
    p.add_argument("--wait", type=float, default=10.0,
                   help="seconds to poll get-version for the first answer, 0 = skip (default 10)")
    args = p.parse_args()
    if not 0 <= args.compression <= 255:
        p.error("--compression must be 0-255")
    try:
        probe(args)
    except ProbeError as e:
        print("FAILED: %s" % e, file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
