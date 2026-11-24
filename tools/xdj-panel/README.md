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
  controller's own deck layer; use A/B for RX decks. SHIFT + FUNCTION/REC
  toggles RX QUANTIZE.
- The ERGO MASTER knob controls RX master volume through the RX mixer. This is
  separate from the FLX4 hardware-output volume mapping and has been confirmed
  on the Pi.
- The ERGO VINYL/PULSE MODE button sends RX VINYL mode changes for decks A/B.
  The hardware sends both deck messages on one press; C/D layer messages are
  not mapped to RX decks.
- The ERGO mixer CUE buttons select RX headphone cue for channels 1 and 2.
  Headphone MIX and LEVEL use their RX mixer controls; these controls have been
  confirmed on the Pi. The cue LEDs mirror the RX panel's reported LED state.
- The ERGO SYNC LEDs mirror the RX panel's per-deck SYNC state, including
  flashing. Hardware verification is still needed.
- The ERGO KEYLOCK buttons control RX MASTER TEMPO on their respective decks;
  their LEDs mirror the RX panel's MASTER TEMPO indicators. LED behavior still
  needs hardware verification.
- The ERGO does not currently have a verified startup position query. Move each
  fader and knob to synchronize it after startup; until then the RX uses its
  own values, which may not match the controller's physical positions.
- The ERGO tempo faders and jogs use RX-specific events; their physical direction,
  jog sensitivity and scratching still need confirmation on the Pi. `TEMPO_REV`
  and `JOG_REV` reverse their respective directions if needed.
- Both ERGO platter-touch notes are mapped to RX jog touch on decks A and B.
  Their effect on scratching during playback still needs hardware verification.
- The ERGO AUTO LOOP knob selects 1/32–32 beats (default 4) while inactive;
  press it to start or exit a loop. While looping, clockwise doubles and
  counterclockwise halves the active loop. The remembered selection may differ
  from a loop made with other controls. Activation, exit and rotation direction
  have been confirmed on the Pi.
- The ERGO's LOOP IN and LOOP OUT LEDs on both decks mirror the RX panel
  indicators, including blinking during a loop.
- The ERGO FX1 ON button controls RX Beat FX on/off. Deck A's FILTER knob
  controls the shared Beat FX level/depth over its full travel: its center is
  approximately 50%, not a neutral filter position. FX1 CONTROL is unmapped on
  RX. On deck A, FX1 selects channel 1 and FX2 selects channel 2; holding
  both together selects master. The selection remains after release. The other
  FX1 assignment buttons retain their deck 2 and master mappings. FX1
  parameter buttons 1 and 2 select the previous and next of eight RX effects.
  Their first press after startup selects the first effect because the current RX
  selection cannot yet be read; changing it elsewhere may desynchronize the
  remembered position. These newer mappings still need hardware verification.
  Other FX1 parameter controls, its fourth assignment button and the FX2 effects
  section are not mapped on RX; their RX3 mappings are suppressed.
- The FX1 ON LED mirrors the RX Beat FX indicator, including its blink. FX1
  parameter button LEDs light while held. The selected assignment stays lit,
  with both deck A FX buttons lit for the master chord. This is controller-side
  history, not live RX state, and starts unlit after a restart.
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
