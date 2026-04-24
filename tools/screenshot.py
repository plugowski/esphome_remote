#!/usr/bin/env python3
"""
ESPHome OLED screenshot tool — SH1106 / SSD1306 128×64 framebuffer → PNG
===========================================================================
Usage
-----
  # Live serial capture (device connected via USB):
  python3 tools/screenshot.py /dev/tty.usbserial-XXXX

  # From a previously saved log file:
  python3 tools/screenshot.py --file dump.log

  # Override output name and upscale factor:
  python3 tools/screenshot.py /dev/tty.usbserial-XXXX -o my_shot.png --scale 6

How it works
------------
On the device, hold OK for 1 s to trigger the `screenshot` script.  It emits:

    I (12345) screenshot: SCREENSHOT_BEGIN
    I (12346) screenshot: <256 hex chars>   ← 128 bytes, 8 lines
    ...
    I (12353) screenshot: SCREENSHOT_END

Buffer layout (SH1106 page-addressing):
  1024 bytes  =  8 pages × 128 columns
  byte index  =  column + page * 128
  pixel (x,y) →  byte[x + (y//8)*128],  bit (y % 8)

Requirements:  pip install pyserial pillow
"""

import argparse
import re
import sys
import time
from pathlib import Path
from typing import List, Optional


# ── helpers ───────────────────────────────────────────────────────────────────

def buf_to_image(buf: bytes, scale: int = 4):
    """Convert raw 1024-byte SH1106 framebuffer to a PIL Image."""
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is required:  pip install pillow")

    W, H = 128, 64
    img = Image.new("1", (W, H), 0)
    pixels = img.load()

    for page in range(8):
        for col in range(W):
            byte = buf[col + page * W]
            for bit in range(8):
                y = page * 8 + bit
                pixels[col, y] = 1 if (byte >> bit) & 1 else 0

    img = img.rotate(180)
    if scale != 1:
        img = img.resize((W * scale, H * scale), Image.NEAREST)

    return img


_ANSI_RE = re.compile(r'\x1b\[[0-9;]*[mGKHF]')


def _strip_ansi(s):
    # type: (str) -> str
    return _ANSI_RE.sub('', s)


def parse_hex_lines(lines: List[str]) -> Optional[bytes]:
    """Extract the 8 payload lines between SCREENSHOT_BEGIN / SCREENSHOT_END."""
    # ESPHome log format: [I][screenshot:NNN]: <hex>
    # No $ anchor — ESPHome appends ANSI colour-reset codes after the data.
    hex_re = re.compile(r"screenshot:\d+\]:\s+([0-9A-Fa-f]{256})(?:[^0-9A-Fa-f]|$)")
    inside = False
    chunks = []  # type: list

    for line in lines:
        if "SCREENSHOT_BEGIN" in line:
            inside = True
            chunks.clear()
            continue
        if "SCREENSHOT_END" in line:
            if inside and len(chunks) == 8:
                return b"".join(chunks)
            inside = False
            continue
        if inside:
            m = hex_re.search(line)
            if m:
                chunks.append(bytes.fromhex(m.group(1)))

    return None


# ── serial capture ────────────────────────────────────────────────────────────

def capture_from_serial(port: str, baud: int, scale: int, output_prefix: str) -> None:
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is required:  pip install pyserial")

    print(f"Listening on {port} @ {baud} baud — hold Pause for 3 s to capture (Ctrl-C to quit)…")
    lines = []  # type: List[str]

    with serial.Serial(port, baud, timeout=0.5) as ser:
        while True:
            raw = ser.readline()
            if not raw:
                continue
            line = _strip_ansi(raw.decode("utf-8", errors="replace").rstrip())
            print(line)
            lines.append(line)
            if "SCREENSHOT_END" in line:
                buf = parse_hex_lines(lines)
                lines = []  # reset for the next capture
                if buf is None:
                    print("  [!] Incomplete capture — ignored.")
                    continue
                if len(buf) != 1024:
                    print(f"  [!] Expected 1024 bytes, got {len(buf)} — ignored.")
                    continue
                out = Path(f"{output_prefix}{int(time.time())}.png")
                img = buf_to_image(buf, scale)
                img.save(out)
                print(f"  >>> Saved {out}  ({img.width}×{img.height} px)")


# ── file capture ──────────────────────────────────────────────────────────────

def capture_from_file(path: str) -> bytes:
    lines = Path(path).read_text(errors="replace").splitlines()
    buf = parse_hex_lines(lines)
    if buf is None:
        sys.exit(f"No complete SCREENSHOT_BEGIN…END block found in {path!r}.")
    return buf


# ── main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert ESPHome serial framebuffer dump to PNG"
    )
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("port", nargs="?", help="Serial port, e.g. /dev/tty.usbserial-XXXX")
    src.add_argument("--file", "-f", metavar="LOG", help="Parse a saved log file instead")

    parser.add_argument("--baud",    "-b", type=int, default=115200)
    parser.add_argument("--output",  "-o", default=None,
                        help="Output PNG path for --file mode (default: screenshot_<timestamp>.png). "
                             "In live serial mode each capture is saved as screenshot_<timestamp>.png.")
    parser.add_argument("--scale",   "-s", type=int, default=4,
                        help="Pixel upscale factor (default 4 → 512×256)")
    args = parser.parse_args()

    if args.file:
        # Single-file mode: parse log, save once, exit.
        raw = capture_from_file(args.file)
        if len(raw) != 1024:
            sys.exit(f"Expected 1024 bytes, got {len(raw)}.")
        img = buf_to_image(raw, args.scale)
        out = Path(args.output if args.output else f"screenshot_{int(time.time())}.png")
        img.save(out)
        print(f"Saved {out}  ({img.width}×{img.height} px)")
    else:
        # Live mode: stay connected, save a new file on every capture.
        try:
            capture_from_serial(args.port, args.baud, args.scale, "screenshot_")
        except KeyboardInterrupt:
            print("\nDone.")


if __name__ == "__main__":
    main()
