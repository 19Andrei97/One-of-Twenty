#!/usr/bin/env python3
"""Stream an X11 application (One Of Twenty) to a browser and forward input.

The game renders into an X display (Xvfb is fine). This process:

  * grabs the root window as frames and serves them as an MJPEG stream,
  * serves a small page that shows the stream,
  * turns browser keyboard/mouse events into XTEST events on that display.

It needs no window manager and no VNC server: only ``python-xlib`` and
``Pillow``. Run it on the same host as the game and open the printed URL.

    python3 tools/stream/stream_server.py --port 12000

Security: this exposes remote control of the X display it is pointed at.
Use it only against a throwaway Xvfb display, and pass ``--password`` when the
port is reachable from an untrusted network (CI tunnels, shared hosts).
"""

from __future__ import annotations

import argparse
import io
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from PIL import Image
from Xlib import X, XK, display
from Xlib.ext import xtest

# Browser ``event.key`` values that do not map to an X keysym by name.
KEY_ALIASES = {
    " ": "space",
    "ArrowUp": "Up",
    "ArrowDown": "Down",
    "ArrowLeft": "Left",
    "ArrowRight": "Right",
    "Escape": "Escape",
    "Enter": "Return",
    "Tab": "Tab",
    "Backspace": "BackSpace",
    "Shift": "Shift_L",
    "Control": "Control_L",
    "Alt": "Alt_L",
    "Meta": "Super_L",
}

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>One Of Twenty &mdash; stream</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { margin: 0; height: 100vh; display: flex; flex-direction: column;
         background: #0d1117; color: #c9d1d9;
         font: 14px/1.4 system-ui, sans-serif; }
  header { display: flex; gap: 12px; align-items: center; padding: 8px 14px;
           background: #161b22; border-bottom: 1px solid #30363d; }
  header h1 { font-size: 14px; font-weight: 600; margin: 0; }
  #status { color: #8b949e; }
  main { flex: 1; display: grid; place-items: center; overflow: hidden;
         padding: 12px; }
  #wrap { position: relative; max-width: 100%; max-height: 100%; }
  #screen { display: block; max-width: 100%; max-height: 100%;
            background: #000; border: 1px solid #30363d; border-radius: 6px;
            image-rendering: pixelated; cursor: crosshair; }
  #capture { position: absolute; inset: 0; display: grid; place-items: center;
             background: rgba(13,17,23,.72); font-size: 15px; text-align: center;
             cursor: pointer; border-radius: 6px; }
  #capture small { display: block; color: #8b949e; margin-top: 6px; }
  footer { padding: 6px 14px; color: #8b949e; background: #161b22;
           border-top: 1px solid #30363d; }
  code { background: #21262d; padding: 1px 5px; border-radius: 4px; }
</style>
</head>
<body>
<header>
  <h1>One Of Twenty</h1>
  <span id="status">connecting&hellip;</span>
</header>
<main>
  <div id="wrap">
    <img id="screen" alt="game stream">
    <div id="capture">Click to take control
      <small>WASD move &middot; M reseed &middot; G wireframe &middot; 1 spawn &middot; P pause</small>
    </div>
  </div>
</main>
<footer>Keys: <code>W A S D</code> <code>M</code> <code>G</code>
  <code>1</code> <code>P</code> &middot; left click paints the tile under the cursor.
</footer>
<script>
(() => {
  const password = "__PW__";
  const img = document.getElementById("screen");
  const capture = document.getElementById("capture");
  const status = document.getElementById("status");
  let focused = false;

  const post = (payload) => {
    const body = JSON.stringify({ ...payload, pw: password });
    fetch("input", { method: "POST", body, keepalive: true });
  };

  img.addEventListener("load", () => { status.textContent = "live"; });
  img.addEventListener("error", () => { status.textContent = "stream lost"; });

  capture.addEventListener("click", (e) => {
    e.preventDefault();
    focused = true;
    capture.style.display = "none";
    img.focus();
    status.textContent = "control active";
  });

  const norm = (e) => {
    const r = img.getBoundingClientRect();
    return { x: (e.clientX - r.left) / r.width, y: (e.clientY - r.top) / r.height };
  };

  window.addEventListener("keydown", (e) => {
    if (!focused || e.repeat) return;
    e.preventDefault();
    post({ type: "key", action: "down", key: e.key });
  });
  window.addEventListener("keyup", (e) => {
    if (!focused) return;
    e.preventDefault();
    post({ type: "key", action: "up", key: e.key });
  });

  img.addEventListener("mousemove", (e) => { if (focused) post({ type: "move", ...norm(e) }); });
  img.addEventListener("mousedown", (e) => {
    if (!focused) return;
    e.preventDefault();
    post({ type: "button", action: "down", button: e.button, ...norm(e) });
  });
  window.addEventListener("mouseup", (e) => {
    if (!focused) return;
    post({ type: "button", action: "up", button: e.button, ...norm(e) });
  });
  img.addEventListener("contextmenu", (e) => e.preventDefault());
  img.addEventListener("wheel", (e) => {
    if (!focused) return;
    e.preventDefault();
    post({ type: "wheel", delta: e.deltaY, ...norm(e) });
  }, { passive: false });
})();
</script>
</body>
</html>
"""


class XController:
    """Apply keyboard/mouse input to an X display through XTEST."""

    def __init__(self, disp: display.Display, screen_size: tuple[int, int]) -> None:
        self._d = disp
        self._w, self._h = screen_size
        self._lock = threading.Lock()

    def _keysym(self, name: str) -> int:
        return XK.string_to_keysym(KEY_ALIASES.get(name, name))

    def key(self, name: str, down: bool) -> None:
        keysym = self._keysym(name)
        if not keysym:
            return
        keycode = self._d.keysym_to_keycode(keysym)
        if not keycode:
            return
        with self._lock:
            xtest.fake_input(self._d, X.KeyPress if down else X.KeyRelease, keycode)
            self._d.sync()

    def _point(self, x: float, y: float) -> tuple[int, int]:
        px = max(0, min(self._w - 1, round(x * (self._w - 1))))
        py = max(0, min(self._h - 1, round(y * (self._h - 1))))
        return px, py

    def move(self, x: float, y: float) -> None:
        px, py = self._point(x, y)
        with self._lock:
            xtest.fake_input(self._d, X.MotionNotify, x=px, y=py)
            self._d.sync()

    def button(self, button: int, down: bool, x: float | None, y: float | None) -> None:
        # Browser buttons: 0 left, 1 middle, 2 right. X: 1 left, 2 middle, 3 right.
        code = {0: 1, 1: 2, 2: 3}.get(button)
        if code is None:
            return
        with self._lock:
            if x is not None and y is not None:
                px, py = self._point(x, y)
                xtest.fake_input(self._d, X.MotionNotify, x=px, y=py)
            xtest.fake_input(self._d, X.ButtonPress if down else X.ButtonRelease, code)
            self._d.sync()

    def wheel(self, delta: float, x: float | None, y: float | None) -> None:
        code = 4 if delta < 0 else 5
        clicks = max(1, min(10, int(abs(delta) // 100) or 1))
        with self._lock:
            if x is not None and y is not None:
                px, py = self._point(x, y)
                xtest.fake_input(self._d, X.MotionNotify, x=px, y=py)
            for _ in range(clicks):
                xtest.fake_input(self._d, X.ButtonPress, code)
                xtest.fake_input(self._d, X.ButtonRelease, code)
            self._d.sync()


class FrameSource:
    """Grab the X root window on a background thread, keep the latest JPEG."""

    def __init__(self, disp: display.Display, quality: int, scale: float, target_fps: int) -> None:
        self._d = disp
        self._root = disp.screen().root
        geo = self._root.get_geometry()
        self.width = geo.width
        self.height = geo.height
        self._quality = quality
        self._scale = scale
        self._interval = 1.0 / max(1, target_fps)
        self._jpeg = b""
        self._version = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="frame-grab", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    def latest(self) -> tuple[bytes, int]:
        return self._jpeg, self._version

    def _capture(self) -> bytes:
        raw = self._root.get_image(0, 0, self.width, self.height, X.ZPixmap, 0xFFFFFFFF)
        image = Image.frombytes("RGB", (self.width, self.height), raw.data, "raw", "BGRX")
        if self._scale != 1.0:
            image = image.resize(
                (max(1, int(self.width * self._scale)), max(1, int(self.height * self._scale))),
                Image.BILINEAR,
            )
        buf = io.BytesIO()
        image.save(buf, format="JPEG", quality=self._quality)
        return buf.getvalue()

    def _run(self) -> None:
        while not self._stop.is_set():
            started = time.monotonic()
            try:
                self._jpeg = self._capture()
                self._version += 1
            except Exception:  # display torn down while streaming
                self._stop.set()
                break
            elapsed = time.monotonic() - started
            if elapsed < self._interval:
                time.sleep(self._interval - elapsed)


class StreamHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "OneOfTwentyStream/1.0"

    # Injected by the server factory.
    frames: FrameSource
    controller: XController
    password: str
    display_name: str

    def log_message(self, fmt: str, *args) -> None:  # keep CI logs readable
        pass

    def do_GET(self) -> None:
        parsed = urlparse(self.path)

        if parsed.path in ("/", "/index.html"):
            pw = parse_qs(parsed.query).get("pw", [""])[0]
            body = PAGE.replace("__PW__", pw).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if parsed.path == "/stream.mjpg":
            self._stream_mjpeg()
            return

        if parsed.path == "/health":
            body = json.dumps({
                "display": self.display_name,
                "size": [self.frames.width, self.frames.height],
                "frames": self.frames.latest()[1],
            }).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_error(404)

    def _stream_mjpeg(self) -> None:
        boundary = "oneoftwentyframe"
        self.send_response(200)
        self.send_header("Content-Type", f"multipart/x-mixed-replace; boundary={boundary}")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()

        delay = 1.0 / 15
        last_version = -1
        try:
            while True:
                jpeg, version = self.frames.latest()
                if jpeg and version != last_version:
                    last_version = version
                    header = (
                        f"--{boundary}\r\nContent-Type: image/jpeg\r\n"
                        f"Content-Length: {len(jpeg)}\r\n\r\n"
                    ).encode()
                    self.wfile.write(header)
                    self.wfile.write(jpeg)
                    self.wfile.write(b"\r\n")
                time.sleep(delay)
        except (BrokenPipeError, ConnectionResetError, OSError):
            return

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path != "/input":
            self.send_error(404)
            return

        length = int(self.headers.get("Content-Length", 0))
        try:
            payload = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            self.send_error(400, "invalid json")
            return

        if self.password and payload.get("pw") != self.password:
            self.send_error(403, "bad password")
            return

        try:
            self._dispatch(payload)
        except Exception as exc:  # never let one bad event kill the stream
            self.log_message("input error: %s", exc)
        self.send_response(204)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def _dispatch(self, payload: dict) -> None:
        kind = payload.get("type")
        x = payload.get("x")
        y = payload.get("y")
        if kind == "key":
            self.controller.key(payload.get("key", ""), payload.get("action") == "down")
        elif kind == "move":
            if x is not None and y is not None:
                self.controller.move(x, y)
        elif kind == "button":
            self.controller.button(payload.get("button", 0), payload.get("action") == "down", x, y)
        elif kind == "wheel":
            self.controller.wheel(payload.get("delta", 0), x, y)


def find_window(disp: display.Display, name: str, timeout: float = 30.0):
    root = disp.screen().root

    def walk(win):
        try:
            if win.get_wm_name() == name:
                return win
        except Exception:
            pass
        try:
            for child in win.query_tree().children:
                found = walk(child)
                if found:
                    return found
        except Exception:
            pass
        return None

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        found = walk(root)
        if found:
            return found
        time.sleep(0.5)
    return None


def main() -> None:
    parser = argparse.ArgumentParser(description="Stream the game to a browser.")
    parser.add_argument("--display", default=":99", help="X display to stream")
    parser.add_argument("--port", type=int, default=12000)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--fps", type=int, default=15)
    parser.add_argument("--quality", type=int, default=70, help="JPEG quality 1-95")
    parser.add_argument("--scale", type=float, default=1.0, help="frame downscale factor")
    parser.add_argument("--window-name", default="One Of Twenty",
                        help="window title to focus; empty to skip")
    parser.add_argument("--password", default="", help="require ?pw=... for all routes")
    args = parser.parse_args()

    disp = display.Display(args.display)
    geo = disp.screen().root.get_geometry()
    controller = XController(disp, (geo.width, geo.height))
    frames = FrameSource(disp, args.quality, args.scale, args.fps)
    frames.start()

    StreamHandler.frames = frames
    StreamHandler.controller = controller
    StreamHandler.password = args.password
    StreamHandler.display_name = args.display

    # Bind and start serving before touching window focus. Focusing is only a
    # convenience for interactive use: walking the window tree can block, and
    # SetInputFocus raises BadMatch for a window that is not viewable (which
    # happens under bare Xvfb), so it must never delay or abort startup.
    server = ThreadingHTTPServer((args.bind, args.port), StreamHandler)
    serving = threading.Thread(target=server.serve_forever, name="http", daemon=True)
    serving.start()
    suffix = f"?pw={args.password}" if args.password else ""
    print(f"streaming {args.display} ({frames.width}x{frames.height}) "
          f"at http://{args.bind}:{args.port}/{suffix}", flush=True)

    if args.window_name:
        try:
            window = find_window(disp, args.window_name)
            if window:
                window.set_input_focus(X.RevertToParent, X.CurrentTime)
                disp.sync()
                print(f"focused window: {args.window_name!r} ({window.id:#x})")
            else:
                print(f"window {args.window_name!r} not found; input follows pointer/focus")
        except Exception as exc:  # focus is best-effort; keep streaming regardless
            print(f"could not focus {args.window_name!r}: {exc}; "
                  "input follows pointer/focus")

    try:
        while serving.is_alive():
            serving.join(timeout=1.0)
    except KeyboardInterrupt:
        pass
    finally:
        frames.stop()
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
