# Optional settings for camera/maix/main.py. Copy to the Maix flash next to main.py
# (/flash/maix_config.py). Without this file main.py uses the values below, which are its defaults.

# Largest JPEG served to the Heltec, in bytes. A bigger frame is encoded again at a lower
# quality until it fits (down to quality 10). 60000 stays under the Heltec V2's 72000-byte buffer.
max_jpeg_bytes = 60000

# Capture at this resolution whatever the Heltec asks for: None (do what the Heltec asks:
# maix_resolution / maix_resolution_v2 in platformio.ini), "VGA" (640x480), "QVGA" (320x240)
# or "QQVGA" (160x120).
framesize = None

# Added to the JPEG quality derived from camera_compression, then clamped to 10-90.
# Negative = smaller, blockier JPEGs.
quality_offset = 0

# Sensor orientation: flip upside down, mirror left-right.
vflip = False
hmirror = False

# Frames thrown away after boot so auto-exposure and white balance settle before the first picture.
warmup_frames = 10
