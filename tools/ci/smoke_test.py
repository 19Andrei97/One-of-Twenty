#!/usr/bin/env python3
"""Headless smoke test for One Of Twenty, runnable in CI.

GitHub Actions runners have no browser to attach to, so this does the part of
"run the game in CI" that is actually possible: it starts Xvfb, runs the game on
it, and verifies that the game renders and reacts to input. With ``--stream`` it
also boots the browser streamer, drives it over HTTP exactly as the web page
does, and pulls one frame out of the MJPEG stream.

Exit code 0 means: the game started, drew a non-blank frame, the frame changed
after injected movement, and (with --stream) the HTTP control path accepted
input and served an image.

    python3 tools/ci/smoke_test.py --binary build/bin/OneOfTwenty --stream

Requires: python-xlib, Pillow, Xvfb. See tools/stream/requirements.txt.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.request
from io import BytesIO
from pathlib import Path

from PIL import Image
from Xlib import X, XK, display
from Xlib.ext import xtest


def start_xvfb(display_name: str, width: int, height: int) -> subprocess.Popen:
    return subprocess.Popen(
        ["Xvfb", display_name, "-screen", "0", f"{width}x{height}x24", "-nolisten", "tcp"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )


def wait_for_display(display_name: str, timeout: float = 10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            return display.Display(display_name)
        except Exception:
            time.sleep(0.2)
    raise RuntimeError(f"X display {display_name} never came up")


def start_game(binary: Path, display_name: str) -> subprocess.Popen:
    env = dict(os.environ, DISPLAY=display_name)
    return subprocess.Popen([str(binary.resolve())], cwd=str(binary.parent), env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)


def find_window(disp, name: str, timeout: float = 30.0):
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
        time.sleep(0.25)
    return None


def grab(disp) -> Image.Image:
    root = disp.screen().root
    geo = root.get_geometry()
    raw = root.get_image(0, 0, geo.width, geo.height, X.ZPixmap, 0xFFFFFFFF)
    return Image.frombytes("RGB", (geo.width, geo.height), raw.data, "raw", "BGRX")


def count_colors(image: Image.Image) -> int:
    return len(image.getcolors(maxcolors=1_000_000) or [])


def wait_for_render(disp, timeout: float = 30.0, min_colors: int = 16) -> Image.Image:
    deadline = time.monotonic() + timeout
    last = grab(disp)
    while time.monotonic() < deadline:
        last = grab(disp)
        if count_colors(last) >= min_colors:
            return last
        time.sleep(0.3)
    return last


def press(disp, keysym: str, down: bool) -> None:
    keycode = disp.keysym_to_keycode(XK.string_to_keysym(keysym))
    xtest.fake_input(disp, X.KeyPress if down else X.KeyRelease, keycode)
    disp.sync()


def changed_ratio(a: Image.Image, b: Image.Image) -> float:
    total = a.size[0] * a.size[1]
    diff = sum(1 for x, y in zip(a.get_flattened_data(), b.get_flattened_data()) if x != y)
    return diff / total


# -- browser streamer checks ------------------------------------------------

def wait_health(url: str, timeout: float = 20.0) -> dict:
    deadline = time.monotonic() + timeout
    last_err = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url + "/health", timeout=3) as r:
                return json.loads(r.read())
        except Exception as exc:
            last_err = exc
            time.sleep(0.5)
    raise RuntimeError(f"streamer never became healthy: {last_err}")


def post_input(url: str, payload: dict) -> int:
    req = urllib.request.Request(url + "/input", data=json.dumps(payload).encode(), method="POST")
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.status


def pull_frame(url: str, timeout: float = 15.0) -> Image.Image:
    """Read one complete JPEG frame out of the MJPEG response."""
    req = urllib.request.Request(url + "/stream.mjpg")
    deadline = time.monotonic() + timeout
    with urllib.request.urlopen(req, timeout=timeout) as r:
        buf = b""
        while time.monotonic() < deadline:
            chunk = r.read(4096)
            if not chunk:
                break
            buf += chunk
            start = buf.find(b"\xff\xd8")
            end = buf.find(b"\xff\xd9", start + 2)
            if start != -1 and end != -1:
                return Image.open(BytesIO(buf[start:end + 2])).convert("RGB")
    raise RuntimeError("no JPEG frame received from MJPEG stream")


def main() -> int:
    parser = argparse.ArgumentParser(description="Headless game smoke test.")
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--display", default=":101")
    parser.add_argument("--window-name", default="One Of Twenty")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--min-colors", type=int, default=16)
    parser.add_argument("--min-changed-ratio", type=float, default=0.01)
    parser.add_argument("--artifacts", default="artifacts")
    parser.add_argument("--stream", action="store_true", help="also test the browser streamer")
    parser.add_argument("--stream-port", type=int, default=12000)
    parser.add_argument("--stream-server", default="tools/stream/stream_server.py", type=Path)
    args = parser.parse_args()

    if not args.binary.exists():
        print(f"FAIL: binary not found: {args.binary}", file=sys.stderr)
        return 2

    artifacts = Path(args.artifacts)
    artifacts.mkdir(parents=True, exist_ok=True)

    xvfb = start_xvfb(args.display, args.width, args.height)
    game = None
    streamer = None
    try:
        disp = wait_for_display(args.display)
        game = start_game(args.binary, args.display)

        window = find_window(disp, args.window_name)
        if window is None:
            print("FAIL: game window never appeared", file=sys.stderr)
            return 1
        window.set_input_focus(X.RevertToParent, X.CurrentTime)
        disp.sync()

        before = wait_for_render(disp, min_colors=args.min_colors)
        colors = count_colors(before)
        before.save(artifacts / "frame-start.png")
        print(f"rendered frame: {colors} distinct colours")
        if game.poll() is not None or colors < args.min_colors:
            print(f"FAIL: game not rendering (exit={game.poll()}, colours={colors})", file=sys.stderr)
            return 1

        press(disp, "d", True)
        time.sleep(1.0)
        press(disp, "d", False)
        time.sleep(0.5)
        after = grab(disp)
        after.save(artifacts / "frame-after-move.png")
        ratio = changed_ratio(before, after)
        print(f"changed pixel ratio after movement: {ratio:.4f}")
        if ratio < args.min_changed_ratio:
            print("FAIL: frame did not change after input", file=sys.stderr)
            return 1

        if args.stream:
            url = f"http://127.0.0.1:{args.stream_port}"
            streamer = subprocess.Popen(
                [sys.executable, str(args.stream_server), "--display", args.display,
                 "--port", str(args.stream_port), "--fps", "10"],
                stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
            )
            health = wait_health(url)
            print(f"streamer healthy: {health}")

            # Drive it exactly like the web page: reseed, toggle wireframe, move.
            for payload in (
                {"type": "key", "action": "down", "key": "m"},
                {"type": "key", "action": "up", "key": "m"},
                {"type": "key", "action": "down", "key": "g"},
                {"type": "key", "action": "up", "key": "g"},
                {"type": "move", "x": 0.5, "y": 0.5},
                {"type": "key", "action": "down", "key": "d"},
            ):
                post_input(url, payload)
                time.sleep(0.2)
            time.sleep(0.8)
            post_input(url, {"type": "key", "action": "up", "key": "d"})
            post_input(url, {"type": "key", "action": "down", "key": "g"})
            post_input(url, {"type": "key", "action": "up", "key": "g"})

            frame = pull_frame(url)
            frame.save(artifacts / "frame-from-stream.png")
            colors = count_colors(frame)
            print(f"stream frame: {frame.size}, {colors} distinct colours")
            if colors < args.min_colors:
                print("FAIL: streamed frame looks blank", file=sys.stderr)
                return 1

        print("OK: game started, rendered and responded to input")
        return 0
    finally:
        for proc in (streamer, game, xvfb):
            if proc and proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()


if __name__ == "__main__":
    sys.exit(main())
