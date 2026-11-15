#!/bin/sh
# check-rx-assets.sh — sanity-check native XDJ-RX asset layout.
#
# Usage:
#   scripts/check-rx-assets.sh [RX_ROOT]
#
# Defaults RX_ROOT to /Documents/xdj-rx-firmware, matching PROFILE=rx.

set -eu

RX_ROOT="${1:-/Documents/xdj-rx-firmware}"

say() { printf '%s\n' "$*"; }
ok() { say "ok   $*"; }
warn() { say "warn $*"; }
fail() { say "FAIL $*"; }

say "checking RX assets at: $RX_ROOT"
if [ ! -d "$RX_ROOT" ]; then
  fail "path does not exist"
  exit 1
fi

rc=0

have() {
  path="$1"
  if [ -e "$path" ]; then
    ok "$path"
    return 0
  fi
  fail "$path (missing)"
  rc=1
  return 1
}

optional_dir() {
  path="$1"
  if [ -d "$path" ]; then
    ok "$path"
  else
    warn "$path (optional for runtime, useful for extraction tooling)"
  fi
}

have "$RX_ROOT/XDJRX-rootfs" || true
have "$RX_ROOT/rbp" || true
have "$RX_ROOT/XDJRX-gui/pset" || true
have "$RX_ROOT/XDJRX-gui/pioneer" || true

if [ -d "$RX_ROOT/XDJRX/gui" ]; then
  ok "$RX_ROOT/XDJRX/gui"
else
  warn "$RX_ROOT/XDJRX/gui (optional; fontdata/imagedata can come from another root via GUI=)"
fi

optional_dir "$RX_ROOT/XDJRX"

if ls "$RX_ROOT"/*.zip >/dev/null 2>&1; then
  say
  say "zip payloads found:"
  ls "$RX_ROOT"/*.zip | sed 's/^/  /'
fi

say
if [ "$rc" -ne 0 ]; then
  say "asset status: INCOMPLETE"
  say "for PROFILE=rx builds, required layout is:"
  say "  XDJRX-rootfs/"
  say "  XDJRX-gui/{pset,pioneer}/"
  say "  rbp"
  exit "$rc"
fi

say "asset status: READY"
exit 0
