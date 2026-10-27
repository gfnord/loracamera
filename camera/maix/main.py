# loracamera: Sipeed Maix Nano (Kendryte K210, MaixPy) as a VC0706 serial JPEG camera.
#
# Answers the VC0706 protocol exactly as lib/VC0706 (Adafruit_VC0706) on the Heltec remote
# node sends it, so the remote firmware drives this camera like the Adafruit 613.
# Copy it to the Maix flash as /flash/main.py, with maix_config.py next to it (optional).
# Setup and tests: docs/maix.md.
#
# Log and timings go to print(), i.e. the USB REPL (IO4/IO5); the camera UART on IO6/IO8
# carries nothing but VC0706 replies. Written for MaixPy-v1 (MicroPython 1.x: no f-strings).

import time
SCRIPT_START_MS = time.ticks_ms()

import gc
import sensor
from machine import UART
from fpioa_manager import fm

# ---- Camera UART, mapped with the FPIOA ----
# IO4/IO5 are the USB/ISP UART and the MaixPy REPL: never use them here.
PIN_TX = 6  # Maix TX -> Heltec CAMERATX_BOARDRX (V2 GPIO23, V4-R8 GPIO47)
PIN_RX = 8  # Maix RX <- Heltec CAMERARX_BOARDTX (V2 GPIO17, V4-R8 GPIO48)
RX_BUFFER = 1024

# ---- VC0706 protocol, as lib/VC0706/Adafruit_VC0706.cpp uses it ----
# Command: 56 00 <cmd> <n> <n argument bytes>. Reply: 76 00 <cmd> <status> <n> <n data bytes>.
CMD_GEN_VERSION = 0x11
CMD_SET_PORT = 0x24
CMD_RESET = 0x26
CMD_READ_DATA = 0x30
CMD_WRITE_DATA = 0x31
CMD_READ_FBUF = 0x32
CMD_GET_FBUF_LEN = 0x34
CMD_FBUF_CTRL = 0x36
# Argument count the library sends with each command. Get-version is the exception: the
# library sends n = 1 and no argument byte, so only n itself is read for it.
ARG_COUNT = {
    CMD_GEN_VERSION: 0,
    CMD_SET_PORT: 3,
    CMD_RESET: 0,
    CMD_READ_DATA: 4,
    CMD_WRITE_DATA: 5,
    CMD_READ_FBUF: 12,
    CMD_GET_FBUF_LEN: 1,
    CMD_FBUF_CTRL: 1,
}
STATUS_OK = 0x00
STATUS_ERROR = 0x04  # any non-zero status makes the library's command fail
FBUF_STOP_CURRENT = 0x00
FBUF_STOP_NEXT = 0x01
FBUF_STEP = 0x02
FBUF_RESUME = 0x03
# Set-port (0x24) divisor -> baud rate, from the library's setBaud*() functions.
BAUDS = {0xAEC8: 9600, 0x56E4: 19200, 0x2AF2: 38400, 0x1C1C: 57600, 0x0DA6: 115200}
DEFAULT_BAUD = 38400  # at power-up and after a reset, like a real VC0706
DEFAULT_COMPRESSION = 0x36  # the VC0706 default, 54; volatile: back to it on reset
# Write/read-data (0x31/0x30) registers the library uses: (type, address).
REG_IMAGE_SIZE = (0x04, 0x0019)
REG_COMPRESSION = (0x01, 0x1204)
# VC0706 image size code -> (MaixPy framesize name, width, height).
FRAMESIZES = {0x00: ('VGA', 640, 480), 0x11: ('QVGA', 320, 240), 0x22: ('QQVGA', 160, 120)}
SIZE_CODES = {'VGA': 0x00, 'QVGA': 0x11, 'QQVGA': 0x22}
# Get-version and set-port replies are padded to the library's 100-byte buffer
# (CAMERABUFFSIZ): it stops reading as soon as that's full, instead of waiting 2 s for silence.
PADDED_DATA = 100 - 5
FBUF_MARK = b'\x76\x00\x32\x00\x00'  # sent before and after the data of each frame-buffer read
MAX_READ = 4096  # longest frame-buffer read served; the library asks for 64 bytes at a time

def fnv1a(data):
    # Same hash the remote firmware logs over the picture it read (fnv1a in
    # src/remote/main.cpp): equal values mean the readout was byte-exact.
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h

# ---- Timing ----
STALE_MS = 50  # a command still incomplete this long after its last byte is dropped
ENCODE_BUDGET_MS = 1500  # stop re-encoding after this: the Heltec waits ~2 s for the length
QUALITY_MIN = 10
QUALITY_MAX = 90

SENSORS = {0x2642: 'OV2640', 0x9d: 'GC0328', 0x7742: 'OV7740', 0x5640: 'OV5640',
           0x3660: 'OV3660', 0x45: 'GC2145', 0x77: 'OV7725', 0x96: 'OV9650'}

CONFIG_DEFAULTS = {
    'max_jpeg_bytes': 60000,
    'framesize': None,
    'quality_offset': 0,
    'vflip': False,
    'hmirror': False,
    'warmup_frames': 10,
}


def log(msg):
    print('[maix %7d ms] %s' % (time.ticks_ms(), msg))


def since(t):
    return time.ticks_diff(time.ticks_ms(), t)


def load_config():
    cfg = dict(CONFIG_DEFAULTS)
    try:
        import maix_config
    except ImportError:
        log('no maix_config.py, using the defaults')
        return cfg
    except Exception as e:
        log('maix_config.py failed to load (%s), using the defaults' % e)
        return cfg
    for key in cfg:
        if hasattr(maix_config, key):
            cfg[key] = getattr(maix_config, key)
    if cfg['framesize'] is not None and cfg['framesize'] not in SIZE_CODES:
        log('maix_config.framesize must be None, "VGA", "QVGA" or "QQVGA"; ignored')
        cfg['framesize'] = None
    log('config: %s' % (cfg,))
    return cfg


def jpeg_quality(register, offset):
    # The compression register works like a quantisation scale with 54 = the camera's own
    # default. Treat 54 as IJG quality 50 (scale 100 %) and invert IJG's quality-to-scale
    # rule: scale = 100 * register / 54; quality = 5000 / scale above 100 %,
    # 100 - scale / 2 below. Then add maix_config.quality_offset and clamp to 10-90.
    if register >= DEFAULT_COMPRESSION:
        q = 2700 // register
    else:
        q = 100 - 50 * register // DEFAULT_COMPRESSION
    return max(QUALITY_MIN, min(QUALITY_MAX, q + offset))


class Camera:
    def __init__(self, cfg):
        self.cfg = cfg
        self.ok = False
        self.name = 'no sensor'
        forced = cfg['framesize']
        self.size = SIZE_CODES[forced] if forced else 0x00
        self.pending = 0  # frames to discard before a picture: warm-up, or a size change
        self.warm_logged = False
        self.frame = None  # frozen RGB565 frame (in the sensor's frame buffer)
        self.jpeg_img = None  # the encoded image; keeps self.jpeg's memory alive
        self.jpeg = None  # memoryview of the JPEG served to the Heltec

    def start(self):
        t = time.ticks_ms()
        for attempt in (1, 2, 3):
            try:
                sensor.reset()
                break
            except Exception as e:
                log('sensor.reset() failed: %s (attempt %d of 3)' % (e, attempt))
                time.sleep_ms(200)
        else:
            log('no camera sensor: pictures will fail')
            return
        sid = sensor.get_id()
        self.name = SENSORS.get(sid, 'sensor 0x%x' % sid)
        try:
            sensor.set_pixformat(sensor.RGB565)
            sensor.set_framesize(getattr(sensor, FRAMESIZES[self.size][0]))
            sensor.set_vflip(bool(self.cfg['vflip']))
            sensor.set_hmirror(bool(self.cfg['hmirror']))
            sensor.run(1)
        except Exception as e:
            log('sensor setup failed: %s' % e)
            return
        self.ok = True
        self.pending = self.cfg['warmup_frames']
        log('sensor %s (id 0x%x) reset in %d ms' % (self.name, sid, since(t)))

    def set_size(self, code):
        forced = self.cfg['framesize']
        if forced:
            log('Heltec asked for %s; maix_config forces %s' % (FRAMESIZES[code][0], forced))
            return
        if code == self.size:
            return
        if self.ok:
            try:
                sensor.set_framesize(getattr(sensor, FRAMESIZES[code][0]))
            except Exception as e:
                log('set_framesize(%s) failed: %s' % (FRAMESIZES[code][0], e))
                return
            self.pending = max(self.pending, 2)  # the first frames after a resize can be torn
        self.size = code
        log('framesize %s' % FRAMESIZES[code][0])

    def live(self):
        # Keep the sensor streaming between commands, like the VC0706's live video:
        # auto-exposure keeps up, and warm-up frames are used up while the Heltec configures.
        try:
            sensor.snapshot()
        except Exception as e:
            log('snapshot failed: %s' % e)
            time.sleep_ms(100)
            return
        if self.pending > 0:
            self.pending -= 1
            if self.pending == 0 and not self.warm_logged:
                self.warm_logged = True
                log('warm-up done: first frame ready')

    def freeze(self):
        if self.frame is not None:
            return True  # already frozen: keep that frame
        if not self.ok:
            return False
        t = time.ticks_ms()
        try:
            while self.pending > 0:
                sensor.snapshot()
                self.pending -= 1
            self.frame = sensor.snapshot()
        except Exception as e:
            log('capture failed: %s' % e)
            self.frame = None
            return False
        log('captured %s RGB565 in %d ms' % (FRAMESIZES[self.size][0], since(t)))
        return True

    def encode(self, quality):
        # Encode the frozen frame once; while the JPEG is over max_jpeg_bytes, encode the same
        # frame again at a lower quality. compressed() leaves the RGB565 frame untouched
        # (compress() would overwrite it with the JPEG).
        limit = self.cfg['max_jpeg_bytes']
        start = time.ticks_ms()
        q = quality
        while True:
            t = time.ticks_ms()
            in_place = False
            try:
                img = self.frame.compressed(quality=q)
            except Exception as e:  # MemoryError when the heap can't hold a second copy
                log('compressed() failed (%s): encoding in place' % e)
                gc.collect()
                img = self.frame.compress(quality=q)
                in_place = True
            data = img.to_bytes()
            size = len(data)
            log('JPEG quality %d: %d bytes in %d ms' % (q, size, since(t)))
            if size <= limit or q <= QUALITY_MIN or in_place:
                break
            if since(start) > ENCODE_BUDGET_MS:
                log('out of time for another encode')
                break
            q = max(QUALITY_MIN, min(q - 5, q * limit * 85 // (size * 100)))
            log('over max_jpeg_bytes (%d): encoding again at quality %d' % (limit, q))
            img = data = None
            gc.collect()
        if size > limit:
            log('JPEG of %d bytes is over max_jpeg_bytes (%d); serving it anyway' % (size, limit))
        self.jpeg_img = img
        self.jpeg = memoryview(data)
        log('JPEG ready in %d ms' % since(start))

    def drop(self):
        if self.frame is None and self.jpeg is None:
            return
        self.frame = self.jpeg = self.jpeg_img = None
        gc.collect()


def padded(cmd, text=b''):
    text = text[:PADDED_DATA]
    return bytes((0x76, 0x00, cmd, STATUS_OK, PADDED_DATA)) + text + b'\x00' * (PADDED_DATA - len(text))


class Emulator:
    def __init__(self, cam, cfg):
        self.cam = cam
        self.cfg = cfg
        self.uart = None
        self.baud = 0
        self.compression = DEFAULT_COMPRESSION
        self.version = padded(CMD_GEN_VERSION, ('VC0706 Maix K210 %s' % cam.name).encode())
        self.commands = 0
        self.read_start = None

    def set_baud(self, baud):
        if baud == self.baud:
            return
        if self.uart is None:
            self.uart = UART(UART.UART1, baud, 8, None, 1, timeout=1000, read_buf_len=RX_BUFFER)
        else:
            # write() returns once the last bytes are in the TX FIFO: let them go out first.
            time.sleep_ms(340000 // self.baud + 2)
            self.uart.init(baud, 8, None, 1, timeout=1000, read_buf_len=RX_BUFFER)
        # MaixPy v0.6.0: the UART constructor and init() clear the FPIOA pin functions,
        # leaving TX unmapped so the remote hears nothing. Register after both.
        fm.register(PIN_TX, fm.fpioa.UART1_TX, force=True)
        fm.register(PIN_RX, fm.fpioa.UART1_RX, force=True)
        self.baud = baud
        log('camera UART at %d baud' % baud)

    def reply(self, cmd, status=STATUS_OK, data=b''):
        self.uart.write(bytes((0x76, 0x00, cmd, status, len(data))) + data)

    def quality(self):
        return jpeg_quality(self.compression, self.cfg['quality_offset'])

    def handle(self, cmd, a):
        if self.commands == 0:
            log('first command (0x%02x) %d ms after main.py started' % (cmd, since(SCRIPT_START_MS)))
        self.commands += 1
        if cmd == CMD_GEN_VERSION:
            self.uart.write(self.version)
        elif cmd == CMD_RESET:
            self.reply(cmd)
            self.cam.drop()
            self.compression = DEFAULT_COMPRESSION
            self.set_baud(DEFAULT_BAUD)
            log('reset')
        elif cmd == CMD_SET_PORT:
            baud = BAUDS.get((a[1] << 8) | a[2]) if a[0] == 0x01 else None
            if baud is None:
                self.reply(cmd, STATUS_ERROR)
                return
            self.uart.write(padded(cmd))
            self.set_baud(baud)
        elif cmd == CMD_WRITE_DATA:
            reg = (a[0], (a[2] << 8) | a[3])
            if a[1] == 1 and reg == REG_IMAGE_SIZE and a[4] in FRAMESIZES:
                self.reply(cmd)
                self.cam.set_size(a[4])
            elif a[1] == 1 and reg == REG_COMPRESSION:
                self.reply(cmd)
                self.compression = a[4]
                log('compression %d -> JPEG quality %d (quality_offset %d)'
                    % (a[4], self.quality(), self.cfg['quality_offset']))
            else:
                self.reply(cmd, STATUS_ERROR)
        elif cmd == CMD_READ_DATA:
            reg = (a[0], (a[2] << 8) | a[3])
            if a[1] == 1 and reg == REG_IMAGE_SIZE:
                self.reply(cmd, STATUS_OK, bytes((self.cam.size,)))
            elif a[1] == 1 and reg == REG_COMPRESSION:
                self.reply(cmd, STATUS_OK, bytes((self.compression,)))
            else:
                self.reply(cmd, STATUS_ERROR)
        elif cmd == CMD_FBUF_CTRL:
            op = a[0]
            if op == FBUF_STOP_CURRENT or op == FBUF_STOP_NEXT:
                if not self.cam.freeze():
                    self.reply(cmd, STATUS_ERROR)
                    return
                self.reply(cmd)
                # Encode after replying: the Heltec only asks for the length ~0.2 s later.
                if self.cam.jpeg is None:
                    self.cam.encode(self.quality())
            elif op == FBUF_RESUME or op == FBUF_STEP:
                self.reply(cmd)
                self.cam.drop()
            else:
                self.reply(cmd, STATUS_ERROR)
        elif cmd == CMD_GET_FBUF_LEN:
            n = len(self.cam.jpeg) if self.cam.jpeg is not None else 0
            self.reply(cmd, STATUS_OK, bytes(((n >> 24) & 0xFF, (n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF)))
        elif cmd == CMD_READ_FBUF:
            self.read_fbuf(a)

    def read_fbuf(self, a):
        # Arguments: frame type, control mode, 32-bit offset, 32-bit length, 16-bit delay.
        jpeg = self.cam.jpeg
        offset = int.from_bytes(a[2:6], 'big')
        length = int.from_bytes(a[6:10], 'big')
        if jpeg is None or length > MAX_READ:
            self.reply(CMD_READ_FBUF, STATUS_ERROR)
            return
        if offset == 0:
            self.read_start = time.ticks_ms()
            gc.collect()  # here, not mid-readout: a pause between a reply's bytes reads
            # as a stall on the Heltec, which drops the chunk and retries.
            log('serving %d bytes, fnv1a %08x' % (len(jpeg), fnv1a(jpeg)))
        total = len(jpeg)
        end = min(offset + length, total)
        missing = length - max(0, end - offset)
        # One write, so the reply reaches the Heltec as an unbroken byte stream.
        self.uart.write(FBUF_MARK + jpeg[offset:end] + b'\x00' * missing + FBUF_MARK)
        if offset + length >= total and self.read_start is not None:
            log('read out %d bytes in %d ms at %d baud' % (total, since(self.read_start), self.baud))
            self.read_start = None

    def parse(self, rx):
        # Run every complete command in rx; return what's left (an incomplete command).
        i = 0
        n = len(rx)
        while True:
            while i < n and rx[i] != 0x56:
                i += 1
            if n - i < 4:
                break
            cmd = rx[i + 2]
            count = ARG_COUNT.get(cmd)
            if rx[i + 1] != 0x00 or count is None:
                i += 1
                continue
            if cmd != CMD_GEN_VERSION and rx[i + 3] != count:
                i += 1
                continue
            if n - i < 4 + count:
                break
            args = rx[i + 4:i + 4 + count]
            i += 4 + count
            try:
                self.handle(cmd, args)
            except Exception as e:
                log('command 0x%02x failed: %s' % (cmd, e))
        return rx[i:]

    def run(self):
        rx = b''
        last_rx = time.ticks_ms()
        while True:
            waiting = self.uart.any()
            if waiting:
                data = self.uart.read(waiting)
                if data:
                    rx = self.parse(rx + data)
                    last_rx = time.ticks_ms()
                continue
            if rx:
                if since(last_rx) > STALE_MS:
                    rx = self.parse(rx[1:])  # drop the stalled start byte
                else:
                    time.sleep_ms(1)
                continue
            if self.cam.ok and self.cam.frame is None:
                self.cam.live()
            else:
                time.sleep_ms(1)


def main():
    log('main.py started')
    cfg = load_config()
    fm.register(PIN_TX, fm.fpioa.UART1_TX, force=True)
    fm.register(PIN_RX, fm.fpioa.UART1_RX, force=True)
    cam = Camera(cfg)
    cam.start()  # before the UART: the version reply names the sensor
    emu = Emulator(cam, cfg)
    emu.set_baud(DEFAULT_BAUD)
    log('answering VC0706 commands on IO%d/IO%d, %d ms after main.py started'
        % (PIN_TX, PIN_RX, since(SCRIPT_START_MS)))
    emu.run()


main()
