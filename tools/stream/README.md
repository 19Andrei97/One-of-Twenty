# Playing One Of Twenty in a browser

There is no WebAssembly build, so the game cannot run *inside* a browser tab.
What works is a thin remote-display stream: run the game on a headless X server
(Xvfb), capture that display as MJPEG, and forward browser keyboard/mouse events
back through XTEST. That is exactly what `stream_server.py` does.

This is useful for:

- playing the game on a remote/container machine,
- showing the running game during development or review,
- letting CI (or a reviewer) drive the game and capture real frames.

## Requirements

- A built game (`cmake --preset default && cmake --build build`).
- `xvfb` (or any X display).
- Python packages from `requirements.txt` (`python-xlib`, `Pillow`):

  ```bash
  python3 -m pip install -r tools/stream/requirements.txt
  ```

## Start it

```bash
# 1. Virtual display sized to the game window (config/config.json: 1280x720).
Xvfb :99 -screen 0 1280x720x24 -nolisten tcp &

# 2. The game, on that display. Run from the dir that holds config/ and fonts/.
cd build/bin && DISPLAY=:99 ./OneOfTwenty &

# 3. The streamer. Pick a port your environment exposes.
python3 tools/stream/stream_server.py --display :99 --port 12000
```

Open `http://<host>:12000/` and click the picture to take control.

On a machine where the port is reachable by others, add a password:

```bash
python3 tools/stream/stream_server.py --display :99 --port 12000 --password hunter2
# open http://<host>:12000/?pw=hunter2
```

## Controls

| Input | In game |
| --- | --- |
| `W` `A` `S` `D` | move the camera |
| `M` | reseed the map |
| `G` | toggle wireframe (hold) |
| `1` | spawn a generic human |
| `P` | pause |
| left click | paint the tile under the cursor (debug) |

Anything the game reads from `sf::Keyboard` / `sf::Mouse` works, because events
are injected at the X server, not patched into the game.

## Notes and limits

- **Security:** this grants remote control of the X display it points at. Only
  point it at a throwaway Xvfb display (as above) — never at your real desktop.
  Use `--password` whenever the port is not private.
- **Latency:** capture is a JPEG-per-frame MJPEG stream, roughly 15 fps by
  default. Tune with `--fps`, `--quality`, `--scale` (e.g. `--scale 0.5` cuts
  bandwidth when the link is slow).
- **No audio, no clipboard.** Input and video only.
- **Windows/macOS:** the streamer is X11-only. The game itself builds on
  Windows; the streamer does not.

## In CI

`tools/ci/smoke_test.py --stream` starts Xvfb, the game and this streamer,
drives the streamer over HTTP and asserts a real frame comes back. See
`.github/workflows/game-smoke.yml`. It is a smoke test, not a way to watch a
runner live — Actions runners are headless and ephemeral.
