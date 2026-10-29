#!/usr/bin/env python3
"""
OLED monitor: a small local web app that shows what a remote built with `platform: fake_ssd1306` would draw.

  python3 tools/oled_monitor.py                         # first /dev/cu.usbmodem* port, http://127.0.0.1:8706
  python3 tools/oled_monitor.py /dev/cu.usbmodemXXXX --http-port 8706

  - the 128x64 screen as a dot matrix, live (frames come from the `oledfb` log lines)
  - battery / USB readings (the "test:" log line of packages/test_log.yaml) with a graph
  - the rest of the serial log, coloured by level

It reconnects by itself when the remote drops off USB (deep sleep, reset). The app holds the serial port, so close
other serial monitors and flashing tools first. Needs: pip install pyserial   (nothing else; the page is plain HTML).
"""

import argparse
import base64
import collections
import glob
import json
import queue
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FRAME_RE = re.compile(r"oled(?:fb|shot):\d+\]:\s+(\d+)\s+(\d)\s+([A-Za-z0-9+/=]{172,176})")
# The firmware's battery line (components/oled_failover + the board package), and the older test-build format.
GAUGE_RE = re.compile(
    r"(?:oledbat(?::\d+)?\]:\s*usb=(\d)\s+mv=(-?\d+)\s+pct=(-?\d+)\s+gauge_v=(\S+)\s+gauge_pct=(\S+))"
    r"|(?:USB=(\w+)\s+battery=(-?\d+) mV\s+(-?\d+) %\s+gauge_v=(\S+) gauge_pct=(\S+))"
)
ANSI_RE = re.compile(r"\x1b?\[[0-9;]*m")


class Hub:
    """Latest state plus a fan-out of events to every connected browser."""

    def __init__(self):
        self.lock = threading.Lock()
        self.clients = []
        self.frame = None
        self.logs = collections.deque(maxlen=400)
        self.gauge = collections.deque(maxlen=900)
        self.status = {"connected": False, "port": ""}
        self._parts = {}
        self._current = None

    def publish(self, kind, data):
        msg = "event: %s\ndata: %s\n\n" % (kind, json.dumps(data))
        with self.lock:
            for q in list(self.clients):
                try:
                    q.put_nowait(msg)
                except queue.Full:
                    pass

    def snapshot(self):
        with self.lock:
            out = [("status", self.status)]
            if self.frame:
                out.append(("frame", self.frame))
            out.append(("gauge_history", list(self.gauge)))
            out.append(("log_history", list(self.logs)))
            return out

    def set_status(self, connected, port):
        with self.lock:
            self.status = {"connected": connected, "port": port}
        self.publish("status", self.status)

    def add_line(self, line):
        m = FRAME_RE.search(line)
        if m:
            return self.add_frame_part(int(m.group(1)), int(m.group(2)), m.group(3))
        text = ANSI_RE.sub("", line).strip()
        if not text:
            return
        g = GAUGE_RE.search(text)
        if g:
            def num(v):
                try:
                    f = float(v)
                    return f if f == f else None
                except ValueError:
                    return None
            v = g.groups()[:5] if g.group(2) is not None else g.groups()[5:]
            sample = {"t": time.time(), "usb": v[0] in ("ON", "1"), "mv": int(v[1]), "pct": int(v[2]),
                      "gauge_v": num(v[3]), "gauge_pct": num(v[4])}
            with self.lock:
                self.gauge.append(sample)
            self.publish("gauge", sample)
            return
        entry = {"t": time.time(), "text": text}
        with self.lock:
            self.logs.append(entry)
        self.publish("log", entry)

    def add_frame_part(self, frame, part, b64):
        if frame != self._current:
            self._current, self._parts = frame, {}
        self._parts[part] = base64.b64decode(b64)
        if len(self._parts) == 8:
            data = base64.b64encode(b"".join(self._parts[i] for i in range(8))).decode()
            self.frame = {"n": frame, "data": data, "t": time.time()}
            self.publish("frame", self.frame)
            self._parts = {}


def serial_loop(hub, port_arg, baud):
    import serial

    # Without a port argument, the first /dev/cu.usbmodem* seen is the one: the remote vanishes from USB whenever it
    # sleeps, and picking again then would land on whatever else is plugged in (the hub, say).
    chosen = port_arg
    while True:
        if not chosen:
            chosen = next(iter(sorted(glob.glob("/dev/cu.usbmodem*"))), "")
        port_name = chosen
        try:
            if not port_name:
                raise serial.SerialException("no port")
            port = serial.Serial()
            port.port, port.baudrate, port.timeout = port_name, baud, 0.5
            port.dtr = port.rts = False  # opening the port must not reset the board
            port.exclusive = True
            port.open()
            hub.set_status(True, port_name)
            buf, first = b"", True
            while True:
                data = port.read(4096)
                if not data:
                    continue
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    if first:  # connected mid-line: what is left of that line is not worth showing
                        first = False
                        continue
                    hub.add_line(line.decode(errors="replace"))
        except (serial.SerialException, OSError):
            hub.set_status(False, port_name)
            time.sleep(0.7)


def make_handler(hub, page):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            if self.path.split("?")[0] == "/events":
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.end_headers()
                q = queue.Queue(maxsize=500)
                for kind, data in hub.snapshot():
                    q.put("event: %s\ndata: %s\n\n" % (kind, json.dumps(data)))
                with hub.lock:
                    hub.clients.append(q)
                try:
                    while True:
                        try:
                            self.wfile.write(q.get(timeout=15).encode())
                        except queue.Empty:
                            self.wfile.write(b": keep-alive\n\n")
                        self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError, OSError):
                    pass
                finally:
                    with hub.lock:
                        if q in hub.clients:
                            hub.clients.remove(q)
                return
            body = page.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    return Handler


PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OLED monitor</title>
<style>
  :root { color-scheme: dark; --bg:#14141c; --card:#1c1c27; --line:#2c2c3b; --fg:#e8e8f0; --dim:#8a8aa0; --acc:#7c6cf0; }
  * { box-sizing: border-box; }
  body { margin:0; background:var(--bg); color:var(--fg); font:14px/1.4 -apple-system,BlinkMacSystemFont,"Inter",sans-serif; }
  header { display:flex; align-items:center; gap:14px; padding:12px 20px; border-bottom:1px solid var(--line); flex-wrap:wrap; }
  header h1 { font-size:15px; margin:0; font-weight:650; }
  .badge { padding:3px 10px; border-radius:999px; font-size:12px; font-weight:600; background:#3a2230; color:#ff8a9a; }
  .badge.on { background:#17362a; color:#6ee7a0; }
  .tools { margin-left:auto; display:flex; gap:14px; align-items:center; font-size:13px; color:var(--dim); flex-wrap:wrap; }
  .tools label { display:flex; gap:6px; align-items:center; cursor:pointer; }
  button { background:var(--card); color:var(--fg); border:1px solid var(--line); border-radius:8px; padding:5px 12px; cursor:pointer; font:inherit; }
  button:hover { border-color:var(--acc); }
  main { display:grid; grid-template-columns: minmax(0,3fr) minmax(300px,2fr); gap:16px; padding:16px 20px; }
  @media (max-width: 1000px) { main { grid-template-columns: 1fr; } }
  .card { background:var(--card); border:1px solid var(--line); border-radius:12px; padding:14px; min-width:0; }
  .card h2 { margin:0 0 10px; font-size:11px; letter-spacing:.07em; text-transform:uppercase; color:var(--dim); font-weight:650; display:flex; justify-content:space-between; }
  #screenWrap { background:#05050a; border-radius:8px; padding:10px; }
  canvas#screen { width:100%; height:auto; display:block; image-rendering:pixelated; }
  .tiles { display:grid; grid-template-columns:repeat(2,1fr); gap:10px; }
  .tile { background:#14141c; border:1px solid var(--line); border-radius:10px; padding:10px 12px; }
  .tile span { display:block; font-size:11px; color:var(--dim); text-transform:uppercase; letter-spacing:.05em; }
  .tile strong { font-size:22px; font-variant-numeric:tabular-nums; }
  .tile.warn strong { color:#fbbf24; } .tile.bad strong { color:#ff8a9a; } .tile.good strong { color:#6ee7a0; }
  canvas#graph { width:100%; height:120px; display:block; margin-top:10px; }
  #log { height:330px; overflow:auto; background:#0e0e15; border-radius:8px; padding:8px 10px; font:12px/1.5 ui-monospace,Menlo,Consolas,monospace; white-space:pre-wrap; word-break:break-all; }
  #log div.E { color:#ff8a9a; } #log div.W { color:#fbbf24; } #log div.D, #log div.V { color:#7a7a92; } #log div.I { color:#cfd0e0; }
  #log .t { color:#5c5c74; margin-right:8px; }
  .full { grid-column:1/-1; }
  .meta { color:var(--dim); font-size:12px; font-weight:400; text-transform:none; letter-spacing:0; }
</style></head>
<body>
<header>
  <h1>OLED monitor</h1><span id="badge" class="badge">waiting for the remote</span><span id="port" class="meta"></span>
  <div class="tools">
    <label><input type="checkbox" id="rot" checked> rotate 180°</label>
    <label>colour <input type="color" id="col" value="#d9e8ff"></label>
    <label>dots <input type="range" id="dot" min="25" max="48" value="40"></label>
    <button id="png">Save PNG</button>
  </div>
</header>
<main>
  <section class="card">
    <h2>Screen <span class="meta" id="fps">no frame yet</span></h2>
    <div id="screenWrap"><canvas id="screen" width="1024" height="512"></canvas></div>
  </section>
  <section class="card">
    <h2>Battery &amp; USB <span class="meta" id="gage">no reading yet</span></h2>
    <div class="tiles">
      <div class="tile" id="tState"><span>Battery</span><strong id="vState">-</strong></div>
      <div class="tile" id="tUsb"><span>USB power</span><strong id="vUsb">-</strong></div>
      <div class="tile"><span>Voltage</span><strong id="vVolt">-</strong></div>
      <div class="tile"><span>Level (reported)</span><strong id="vPct">-</strong></div>
      <div class="tile"><span>Gauge raw voltage</span><strong id="vGv">-</strong></div>
      <div class="tile"><span>Gauge raw level</span><strong id="vGp">-</strong></div>
    </div>
    <canvas id="graph" width="600" height="120"></canvas>
  </section>
  <section class="card full">
    <h2>Log <span><label class="meta"><input type="checkbox" id="follow" checked> follow</label> <button id="clear">Clear</button></span></h2>
    <div id="log"></div>
  </section>
</main>
<script>
const $ = (id) => document.getElementById(id);
const S = 8, W = 128, H = 64;
let frame = null, gauge = [], frameTimes = [];

function drawScreen() {
  const c = $('screen'), g = c.getContext('2d');
  g.fillStyle = '#05050a'; g.fillRect(0, 0, c.width, c.height);
  const rot = $('rot').checked, r = S * ($('dot').value / 100), color = $('col').value;
  const buf = frame ? Uint8Array.from(atob(frame.data), ch => ch.charCodeAt(0)) : null;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const sx = rot ? W - 1 - x : x, sy = rot ? H - 1 - y : y;
    const on = buf && (buf[sx + (sy >> 3) * W] >> (sy & 7)) & 1;
    g.beginPath(); g.arc(x * S + S / 2, y * S + S / 2, r, 0, 6.2832);
    if (on) { g.fillStyle = color; g.shadowColor = color; g.shadowBlur = 6; } else { g.fillStyle = 'rgba(255,255,255,0.055)'; g.shadowBlur = 0; }
    g.fill();
  }
  g.shadowBlur = 0;
}

function drawGraph() {
  const c = $('graph'), g = c.getContext('2d'), w = c.width, h = c.height;
  g.clearRect(0, 0, w, h);
  const pts = gauge.filter(s => s.mv > 0);
  g.fillStyle = '#8a8aa0'; g.font = '11px sans-serif';
  if (pts.length < 2) { g.fillText(gauge.length ? 'no battery voltage to plot' : 'waiting for readings', 8, 16); return; }
  const t0 = pts[0].t, t1 = pts[pts.length - 1].t || t0 + 1;
  let lo = Math.min(...pts.map(s => s.mv)), hi = Math.max(...pts.map(s => s.mv));
  lo = Math.min(lo, 3300); hi = Math.max(hi, 4250);
  const X = t => 8 + (w - 16) * (t - t0) / Math.max(t1 - t0, 1), Y = v => h - 16 - (h - 30) * (v - lo) / (hi - lo);
  g.strokeStyle = '#2c2c3b'; g.lineWidth = 1;
  [3300, 3700, 4200].forEach(v => { g.beginPath(); g.moveTo(0, Y(v)); g.lineTo(w, Y(v)); g.stroke(); g.fillText((v / 1000).toFixed(1) + ' V', 8, Y(v) - 3); });
  g.strokeStyle = '#7c6cf0'; g.lineWidth = 2; g.beginPath();
  pts.forEach((s, i) => i ? g.lineTo(X(s.t), Y(s.mv)) : g.moveTo(X(s.t), Y(s.mv))); g.stroke();
  g.fillStyle = '#8a8aa0'; g.fillText(Math.round(t1 - t0) + ' s', w - 44, h - 3);
}

function showGauge(s) {
  const noBat = s.mv === 0 && s.pct < 0;
  const st = $('tState'); st.className = 'tile ' + (noBat ? 'bad' : s.pct < 15 ? 'warn' : 'good');
  $('vState').textContent = noBat ? 'NO BATTERY' : s.pct + ' %';
  $('tUsb').className = 'tile ' + (s.usb ? 'good' : '');
  $('vUsb').textContent = s.usb ? 'ON' : 'off';
  $('vVolt').textContent = noBat ? '-' : (s.mv / 1000).toFixed(3) + ' V';
  $('vPct').textContent = s.pct < 0 ? 'unknown' : s.pct + ' %';
  $('vGv').textContent = s.gauge_v == null ? '-' : s.gauge_v.toFixed(3) + ' V';
  $('vGp').textContent = s.gauge_pct == null ? '-' : s.gauge_pct.toFixed(1) + ' %';
  $('gage').textContent = new Date(s.t * 1000).toLocaleTimeString();
}

function addLog(e) {
  const box = $('log'), d = document.createElement('div');
  const m = /^\[([A-Z])\]/.exec(e.text); d.className = m ? m[1] : 'I';
  const t = document.createElement('span'); t.className = 't'; t.textContent = new Date(e.t * 1000).toLocaleTimeString();
  d.append(t, e.text); box.appendChild(d);
  while (box.childElementCount > 400) box.firstChild.remove();
  if ($('follow').checked) box.scrollTop = box.scrollHeight;
}

const es = new EventSource('/events');
es.addEventListener('status', e => {
  const s = JSON.parse(e.data); $('badge').textContent = s.connected ? 'connected' : 'waiting for the remote (asleep or unplugged)';
  $('badge').className = 'badge' + (s.connected ? ' on' : ''); $('port').textContent = s.port;
});
es.addEventListener('frame', e => {
  frame = JSON.parse(e.data); frameTimes.push(Date.now()); frameTimes = frameTimes.filter(t => Date.now() - t < 5000);
  $('fps').textContent = 'frame ' + frame.n + ' · ' + (frameTimes.length / 5).toFixed(1) + ' fps'; drawScreen();
});
es.addEventListener('gauge', e => { const s = JSON.parse(e.data); gauge.push(s); if (gauge.length > 900) gauge.shift(); showGauge(s); drawGraph(); });
es.addEventListener('gauge_history', e => { gauge = JSON.parse(e.data); if (gauge.length) showGauge(gauge[gauge.length - 1]); drawGraph(); });
es.addEventListener('log', e => addLog(JSON.parse(e.data)));
es.addEventListener('log_history', e => { $('log').innerHTML = ''; JSON.parse(e.data).forEach(addLog); });
['rot', 'col', 'dot'].forEach(id => $(id).addEventListener('input', drawScreen));
$('clear').onclick = () => { $('log').innerHTML = ''; };
$('png').onclick = () => { const a = document.createElement('a'); a.download = 'oled-' + Date.now() + '.png'; a.href = $('screen').toDataURL('image/png'); a.click(); };
drawScreen(); drawGraph();
</script></body></html>
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", help="serial port (default: the first /dev/cu.usbmodem*)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--http-port", type=int, default=8706)
    args = ap.parse_args()
    hub = Hub()
    threading.Thread(target=serial_loop, args=(hub, args.port, args.baud), daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", args.http_port), make_handler(hub, PAGE))
    print("OLED monitor on http://127.0.0.1:%d  (Ctrl+C to quit)" % args.http_port, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
