"""
server.py

Bridges the Arduino over USB serial and serves the web UI.

Usage:
    pip install pyserial
    python server.py              # auto-detects Arduino port
    python server.py COM3         # or specify port (Windows: COM3 / Mac+Linux: /dev/ttyUSB0)

Log is saved to dog_door_log.json alongside this script.
Web UI:  http://localhost:8080
"""

import sys
import json
import time
import threading
import os
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial not found — run:  pip install pyserial")
    sys.exit(1)

# ── config ────────────────────────────────────────────────────────────────────

PORT     = int(os.environ.get("PORT", 8080))
BAUD     = 9600
BASE_DIR = os.path.dirname(os.path.abspath(__file__))   # always the script's own directory
LOG_FILE = os.path.join(BASE_DIR, "dog_door_log.json")

# ── persistent log ────────────────────────────────────────────────────────────

def load_log():
    if os.path.exists(LOG_FILE):
        with open(LOG_FILE, encoding="utf-8") as f:
            try:
                return json.load(f)
            except (json.JSONDecodeError, ValueError):
                return []
    return []

def save_log(entries):
    # write to a temp file then rename — prevents corruption if interrupted
    tmp = LOG_FILE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(entries, f, indent=2)
    os.replace(tmp, LOG_FILE)

log_lock    = threading.Lock()
log_entries = load_log()

def append_log(event: str, uid: str = ""):
    entry = {
        "time":  datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "event": event,
        "uid":   uid,
    }
    with log_lock:
        log_entries.append(entry)
        save_log(log_entries)
    print(f"[{entry['time']}] {event}" + (f"  uid={uid}" if uid else ""))

# ── serial connection ─────────────────────────────────────────────────────────

_arduino      = None          # serial.Serial instance or None
_arduino_lock = threading.Lock()   # guards all reads, writes, and reassignment

def get_arduino():
    with _arduino_lock:
        return _arduino

def find_arduino_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").lower()
        if any(k in desc or k in hwid for k in ["arduino", "ch340", "cp210", "ftdi", "2341"]):
            return p.device
    return ports[0].device if ports else None

def connect_serial(port=None):
    global _arduino
    if port is None:
        port = find_arduino_port()
    if port is None:
        print("no serial port found — running web-only (no Arduino)")
        return
    try:
        ser = serial.Serial(port, BAUD, timeout=1)
        time.sleep(2)   # wait for Arduino reset after DTR pulse
        with _arduino_lock:
            _arduino = ser
        print(f"connected to Arduino on {port}")
        _send("PONG")   # handshake — must happen after lock is set
    except serial.SerialException as e:
        print(f"could not open {port}: {e}")

def _send(msg: str):
    """send a line to the Arduino — call only when _arduino_lock is already held,
       or acquire it here if called from outside serial_loop."""
    with _arduino_lock:
        a = _arduino
        if a and a.is_open:
            try:
                a.write((msg + "\n").encode())
            except (serial.SerialException, OSError):
                pass

def send_to_arduino(msg: str):
    """public send — safe to call from any thread."""
    _send(msg)

# ── serial read loop ──────────────────────────────────────────────────────────

def serial_loop():
    global _arduino
    while True:
        a = get_arduino()
        if a is None or not a.is_open:
            time.sleep(1)
            continue
        try:
            with _arduino_lock:
                line = a.readline()
            line = line.decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            if line == "PING":
                # Arduino rebooted — re-handshake
                _send("PONG")

            elif line.startswith("OPEN|"):
                uid = line.split("|", 1)[1]
                append_log("opened", uid)

            elif line.startswith("DENIED|"):
                uid = line.split("|", 1)[1]
                append_log("denied", uid)

            elif line == "CLOSED":
                append_log("closed")

        except (serial.SerialException, OSError):
            print("Arduino disconnected")
            with _arduino_lock:
                _arduino = None
            time.sleep(3)

# ── http handler ──────────────────────────────────────────────────────────────

class Handler(BaseHTTPRequestHandler):

    def log_message(self, *_):
        pass   # silence default per-request stdout noise

    def do_GET(self):
        if self.path == "/":
            self._serve_file("index.html", "text/html; charset=utf-8")
        elif self.path == "/log":
            with log_lock:
                data = json.dumps(log_entries).encode()
            self._json(200, data)
        elif self.path == "/status":
            a = get_arduino()
            connected = a is not None and a.is_open
            self._json(200, json.dumps({"arduino": connected}).encode())
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path == "/override":
            send_to_arduino("OVERRIDE")
            append_log("override", "web-ui")
            self._json(200, b'{"ok":true}')
        else:
            self.send_error(404)

    def _json(self, code, data: bytes):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _serve_file(self, filename: str, mime: str):
        path = os.path.join(BASE_DIR, filename)
        if not os.path.exists(path):
            self.send_error(404, f"{filename} not found — ensure it is in the same folder as server.py")
            return
        with open(path, "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

# ── entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    port_arg = sys.argv[1] if len(sys.argv) > 1 else None
    connect_serial(port_arg)

    threading.Thread(target=serial_loop, daemon=True).start()

    print(f"web UI → http://localhost:{PORT}")
    try:
        HTTPServer(("", PORT), Handler).serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
