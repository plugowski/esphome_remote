#!/usr/bin/env python3
"""
Live OLED preview in the terminal, for a remote built with `platform: fake_ssd1306`
(or any log stream with `oledfb` frame lines).

  python3 tools/oled_console.py /dev/cu.usbmodemXXXX          # live, Ctrl+C to quit
  python3 tools/oled_console.py /dev/cu.usbmodemXXXX --once   # print the first frame and stop
  esphome logs remote.yaml | python3 tools/oled_console.py -  # any log on stdin

The non-frame log lines (battery, hub, errors) are kept in a small tail under the picture.
Needs: pip install pyserial      Terminal: 130+ columns shows every pixel (--mode half); narrower terminals get --mode fit, the picture scaled to the width
at its true 2:1 shape (needs a truecolor terminal). --mode quad / braille keep 64 columns but distort.
"""

import argparse
import base64
import collections
import re
import shutil
import sys
import time

W, H = 128, 64
FRAME_RE = re.compile(r"oled(?:fb|shot):\d+\]:\s+(\d+)\s+(\d)\s+([A-Za-z0-9+/=]{172,176})")
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def pixels(buf, rotate):
    """128x64 list of rows (True = lit). The buffer is SH1106 pages; the panel is mounted 180 degrees turned."""
    rows = [[False] * W for _ in range(H)]
    for page in range(8):
        for col in range(W):
            byte = buf[col + page * W]
            for bit in range(8):
                x, y = (W - 1 - col, H - 1 - (page * 8 + bit)) if rotate else (col, page * 8 + bit)
                rows[y][x] = bool((byte >> bit) & 1)
    return rows


def render_half(rows):
    glyph = {(0, 0): " ", (1, 0): "▀", (0, 1): "▄", (1, 1): "█"}
    return ["".join(glyph[(rows[y][x], rows[y + 1][x])] for x in range(W)) for y in range(0, H, 2)]


def render_quad(rows):
    """2x2 pixels per character with quadrant blocks: full-cell glyphs, so no gaps between the lines (braille has them)."""
    glyph = " \u2598\u259d\u2580\u2596\u258c\u259e\u259b\u2597\u259a\u2590\u259c\u2584\u2599\u259f\u2588"
    return ["".join(glyph[rows[y][x] | rows[y][x + 1] << 1 | rows[y + 1][x] << 2 | rows[y + 1][x + 1] << 3]
                    for x in range(0, W, 2)) for y in range(0, H, 2)]


def render_fit(rows, cols):
    """The picture scaled to `cols` columns at its true 2:1 shape: one pixel per column, two per line (upper half block
    with 24-bit grey for the top pixel and its background for the bottom one). Pixels are averaged, so thin strokes
    fade instead of vanishing. Returns (lines, width)."""
    tw = max(32, min(W, cols))
    th = max(2, round(tw / 2) // 2 * 2)
    xs = [(i * W // tw, max((i + 1) * W // tw, i * W // tw + 1)) for i in range(tw)]
    ys = [(j * H // th, max((j + 1) * H // th, j * H // th + 1)) for j in range(th)]

    def level(j, i):
        (x0, x1), (y0, y1) = xs[i], ys[j]
        n = sum(rows[y][x] for y in range(y0, y1) for x in range(x0, x1))
        grey = min(1.0, 1.7 * n / ((x1 - x0) * (y1 - y0)))
        return int(255 * grey)

    lines = []
    for j in range(0, th, 2):
        cells = []
        for i in range(tw):
            top, bottom = level(j, i), level(j + 1, i)
            cells.append("\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm\u2580" % (top, top, top, bottom, bottom, bottom))
        lines.append("".join(cells) + "\x1b[0m")
    return lines, tw


def render_braille(rows):
    dots = {(0, 0): 0x01, (0, 1): 0x02, (0, 2): 0x04, (1, 0): 0x08, (1, 1): 0x10, (1, 2): 0x20, (0, 3): 0x40, (1, 3): 0x80}
    return ["".join(chr(0x2800 + sum(v for (dx, dy), v in dots.items() if rows[y + dy][x + dx])) for x in range(0, W, 2))
            for y in range(0, H, 4)]


def draw(frame_no, buf, mode, rotate, tail, fps):
    rows = pixels(buf, rotate)
    if mode == "fit":
        art, width = render_fit(rows, shutil.get_terminal_size((80, 24)).columns - 2)
    else:
        art = {"half": render_half, "quad": render_quad, "braille": render_braille}[mode](rows)
        width = len(art[0])
    out = ["\x1b[H\x1b[2J\x1b[97m", "┌" + "─" * width + "┐"]
    out += ["│" + line + "│" for line in art]
    out += ["└" + "─" * width + "┘\x1b[0m", f"frame {frame_no}   {fps:4.1f} fps   Ctrl+C to quit", ""]
    cols = shutil.get_terminal_size((width + 2, 24)).columns
    out += [line[:cols] for line in tail]
    sys.stdout.write("\n".join(out) + "\n")
    sys.stdout.flush()


def lines_from(source, baud):
    if source == "-":
        for line in sys.stdin:
            yield line
        return
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is required:  pip install pyserial")
    # The remote drops off USB whenever it resets or sleeps: wait for the port and carry on when it is back.
    waiting = False
    while True:
        try:
            port = serial.Serial()
            port.port, port.baudrate, port.timeout = source, baud, 0.5
            port.dtr = port.rts = False  # do not reset the board by opening the port
            port.open()
            if waiting:
                waiting = False
                yield "-- reconnected to " + source
            buf = b""
            while True:
                data = port.read(4096)
                if not data:
                    continue
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    yield line.decode(errors="replace")
        except (serial.SerialException, OSError):
            if not waiting:
                waiting = True
                yield "-- " + source + " is gone (the remote reset or went to sleep), waiting for it..."
            time.sleep(0.5)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="serial port, or - for a log on stdin")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--mode", choices=["half", "fit", "quad", "braille"], default=None,
                    help="half blocks (128 columns, full detail), fit (scaled to the terminal width, true shape), "
                         "quad / braille (64 columns, but stretched tall / with line gaps)")
    ap.add_argument("--no-rotate", action="store_true", help="do not turn the picture 180 degrees")
    ap.add_argument("--once", action="store_true", help="print one frame and exit")
    ap.add_argument("--tail", type=int, default=6, help="log lines kept under the picture")
    args = ap.parse_args()
    mode = args.mode or ("half" if shutil.get_terminal_size((200, 24)).columns >= 132 else "fit")

    parts, current, tail, stamps = {}, None, collections.deque(maxlen=args.tail), collections.deque(maxlen=8)
    try:
        for raw in lines_from(args.source, args.baud):
            line = ANSI_RE.sub("", raw).rstrip()
            m = FRAME_RE.search(line)
            if not m:
                if line.strip():
                    tail.append(line)
                continue
            frame, part, data = int(m.group(1)), int(m.group(2)), base64.b64decode(m.group(3))
            if frame != current:
                current, parts = frame, {}
            parts[part] = data
            if len(parts) == 8:
                stamps.append(time.time())
                fps = (len(stamps) - 1) / (stamps[-1] - stamps[0]) if len(stamps) > 1 and stamps[-1] > stamps[0] else 0.0
                draw(frame, b"".join(parts[i] for i in range(8)), mode, not args.no_rotate, list(tail), fps)
                parts = {}
                if args.once:
                    return
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
