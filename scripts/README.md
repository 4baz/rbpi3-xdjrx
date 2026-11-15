# scripts/

Host-side build tooling and the on-device scripts for the Raspberry Pi ports.

```
scripts/
├── build-chroot.sh        assemble the soft-float deploy root (PROFILE=rx3|rx)
├── check-rx-assets.sh     validate PROFILE=rx asset layout before build
├── patch-rbp-nopc.py      second-stage rbp patch (getPcController NULL deref)
├── patch-rbp-depth.py     third-stage rbp patch (layer pixel format / depth pair)
├── patch-rx-rbp.py        native RX rbp characterization + guarded patch entry
├── device/                scripts that run on the Pi
└── shims/                 LD_PRELOAD shim sources (soft-float ARM32) + Makefile
```

## `build-chroot.sh`

Runs on a Linux/WSL host. Stages the selected profile's rootfs + gui + player +
the shims + the DirectFB 1.4.16 stack + `directfbrc` + touch calibration +
`rb.conf`, fixes exec bits, and tars the result for the Pi's `install.sh` to
untar into `/opt/rblive4`.

Profiles:

- `PROFILE=rx3` (default): existing XDJ-RX3 flow (`RX3=` root, default
  `extracted/`, output `work/rbpi4b-pi4.tgz`).
- `PROFILE=rx`: native XDJ-RX scaffold (`RX=` root, default
  `/Documents/xdj-rx-firmware`, output `work/rbpi4b-pi3-rx.tgz`). Runtime
  audio auto-selects a connected DDJ/ERGO USB card when `RB_AUDIO_DEV` is unset;
  if none is found it falls back to Pi HDMI (`vc4hdmi0`). The default framebuffer
  lie depth is `16` in RX profile (vs `32` in RX3 profile).

Override asset paths with `PROFILE=` and `RX3=` or `RX=`, plus `DFB=` / `OUT=`.
For `PROFILE=rx`, set `RX_REQUIRE_KNOWN_RBP=1` to make unknown RX binary hashes
fatal instead of copy-through.

Before a `PROFILE=rx` build:

```sh
scripts/check-rx-assets.sh /path/to/xdj-rx-firmware
```

See [docs/05](../docs/05-chroot.md).

## `patch-rbp-nopc.py`

Applies the `getPcController()` NULL-deref fix on top of the shared `rbp-audio`:

```sh
python3 patch-rbp-nopc.py rbp-audio -o rbp-nopc
```

The name says what it does, not which device it is for: the RX3 build dereferences
a controller object that does not exist on any non-Pioneer target, and the Pi
fails the same board check the previous target did.

See [docs/03](../docs/03-port-plan.md).

## `patch-rbp-depth.py`

Sets rbp's DirectFB layer pixel format — `DSPF_RGB16` (the RX3's own depth) or
`DSPF_RGB32` — which is two instructions in `DS_HW_Core_Layer_Create` and the
whole of the difference between the deck-1 strip frame's right vertical edge
being drawn whole and being drawn at 1/4 and 1/8 coverage:

```sh
python3 patch-rbp-depth.py rbp-nopc                  # -> 32 bpp, in place
python3 patch-rbp-depth.py rbp-nopc --bpp 16         # -> back to 16 bpp
```

**This is half of a pair and cannot choose for itself.** The other half is
`RB_FB_LIE_BPP` in `rb.conf`, the depth the framebuffer shim reports in
`FBIOGET_VSCREENINFO`; a mismatch makes `DS_HW` refuse the layer and kills rbp
before it paints, so `build-chroot.sh` reads that variable out of the `rb.conf` it
embeds and `start-rb.sh` re-checks the pair before every launch. Idempotent in
both directions, and it validates the word at every address, so it aborts on a
foreign binary instead of corrupting it.

See [PATCHES.md § 12](../tools/patch-rbp/PATCHES.md).

## `patch-rx-rbp.py`

Characterizes an RX `rbp` binary (ELF identity + hashes) and only applies patch
words for explicitly known binary fingerprints. Unknown binaries are copied
through unchanged and can be rejected by passing `--require-known`.

## `device/`

On-device scripts — see [`device/README.md`](device/README.md). The entry point
is `install.sh`; the launcher is `start-rb.sh`.

## `shims/`

The LD_PRELOAD shims — see [`shims/README.md`](shims/README.md). Four are built:
`fbshim.so` (fb ioctls + the fake tsc2007 + pointing), `knobshim.so` (controls:
the rbp bridge, the LED/meter paths and the MIDI maps), `audioshim.so` (rbp's
ALSA contract onto the configured card) and `crashcatch.so` (SIGSEGV reporting
for bring-up). In `PROFILE=rx`, fbshim also applies a tiny in-process guard for
the known low-pointer startup crash path in native RX `rbp` on Pi bring-up.
