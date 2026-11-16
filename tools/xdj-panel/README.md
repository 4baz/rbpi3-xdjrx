# XDJ-RX virtual panel

A small local web app for driving the emulated XDJ-RX panel by hand — buttons,
tempo/channel faders, EQ, beat-FX — laid out like the physical unit, with a **live
view of the QEMU screen** so you see each control react. Use it to test the mapped
controls and to discover the unmapped ones (press, watch, note the frame offset/bit
shown in the log).

## Run (QEMU emulator)

1. Boot the emulator so its **monitor socket** is at `/tmp/d_mon4.sock` and it reads
   `/tmp/subucom_ctrl` (the standard launch: `PORT=1234 SUF=4 … launch_v2.sh`, with
   `XDJUSB=test/usb/rekordbox_test.img` if you want the browser/USB path). Wait for the
   deck UI (not the splash).
2. Start the bridge and open it:
   ```
   python3 tools/xdj-panel/server.py           # --port 8765  --mon /tmp/d_mon4.sock
   open http://127.0.0.1:8765
   ```

The bridge serves the panel, turns clicks/drags into the injection protocol
(`/tmp/subucom_ctrl`), and streams the framebuffer (via the monitor socket) as the
live screen.

## Run (Pi runtime bridge)

On the RX profile, `fbshim` emulates the sub-ucom panel device and generates
CRC-valid RX frames from `/tmp/subucom_ctrl`. The RX3 profile continues to use
the existing direct-key debug bridge. The RX `/tmp/mainkey` translations are
experimental; the source buttons are GPIO inputs and are not yet faithfully
emulated on the Pi.

Start the web panel on the Pi:

```
python3 /opt/rblive4/tools/xdj-panel/server.py --host 0.0.0.0 --port 8765
```

Then open `http://<pi-ip>:8765` from another machine. The server has no
authentication: only expose it on a trusted network.

Notes:
- On Pi there is no QEMU monitor socket, so the live screen pane reports `no QEMU`;
  watch the Pi's attached display instead.
- RX browser rotation and browse push have been checked against the Pi display.
  Other controls, including source buttons, require further verification.
- With the DDJ-ERGO RX map, FUNCTION/REC opens Browse. Deck C/D switch the
  controller's own deck layer; use A/B for RX decks.
- The ERGO MASTER knob controls RX master volume through the RX mixer. This is
  separate from the FLX4 hardware-output volume mapping and has been confirmed
  on the Pi.
- The ERGO tempo faders and jogs use RX-specific events; their physical direction,
  jog sensitivity and scratching still need confirmation on the Pi. `TEMPO_REV`
  and `JOG_REV` reverse their respective directions if needed.
- The ERGO AUTO LOOP knob selects 1/32–32 beats (default 4) while inactive;
  press it to start or exit a loop. While looping, clockwise doubles and
  counterclockwise halves the active loop. The remembered selection may differ
  from a loop made with other controls. Activation, exit and rotation direction
  have been confirmed on the Pi.
- The legacy `sweep_pi_paneldbg.py` checks the direct-key bridge and does not
  validate the RX frame transport.

## Using it

- **Buttons** hold while the mouse is down and release on mouse-up (a real press/hold).
- **Faders / knobs** (range sliders) hold at their last position.
- The panel maintains browse and input-select state alongside momentary controls.
  **Release all** clears momentary controls.
- The **log** shows the exact spec sent for every action (e.g. `→ B 12 08`), so when an
  unverified control does something, you know its frame offset/bit.
- **Probe** panel: hold any bit of any byte, sweep a 16-bit analog, or send a raw spec
  (`B <off> <bit>` hold button · `A <off> <val>` hold analog · lowercase = one-shot; hex).

## Mapping status (colour)

- 🟢 **verified** (green underline) — confirmed on screen: TEMPO faders & TEMPO RANGE
  (both decks), BEAT FX ROLL/TAP. See [`docs/xdj-rx-control-map.md`](../../docs/xdj-rx-control-map.md).
- 🟡 **best-guess** (amber) — plausible offsets/bits not yet confirmed (deck transport,
  mixer, browser/nav, most beat-FX). Press them and watch the screen to confirm or
  correct; audio-only controls (EQ/faders/crossfader/mic) won't change the screen — those
  need the gdb event-code method noted in the control-map doc.

## Protocol

`POST /ctrl` body `"B 12 08"` → writes the spec to `/tmp/subucom_ctrl`.
`POST /release` → removes it. `GET /raw` → the current framebuffer as **raw RGB**
(headers `X-Width`/`X-Height`, body `w*h*3` bytes) drawn straight onto a `<canvas>`.
No per-frame image encoding and a persistent monitor connection, so the grab is ~9 ms
(≈100 fps server-side; the browser caps it at the display refresh). The harness (QEMU
`xdj-subucom`, `qemu/patches/02`) interprets the spec each frame.
