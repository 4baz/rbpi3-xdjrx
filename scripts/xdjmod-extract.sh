#!/bin/sh
# Extract libxdjmod.so from an xdj-rx-tools .xdjpatch (https://github.com/thomas-alrek/xdj-rx-tools)
# and verify it against the patch manifest.
#   usage: xdjmod-extract.sh xdjrx-3.00.xdjpatch OUT_DIR
# Then copy OUT_DIR/libxdjmod.so to /opt/rblive4/ on the Pi and set RB_XDJMOD=1
# in rb.local.conf. The mod targets the stock 2.21 rbp, which is what this
# project runs.
set -eu
patch=${1:?patch file}
out=${2:?output dir}
mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tar -xf "$patch" -C "$tmp" manifest.json files/pioneer/pioneer/libxdjmod.so
want=$(sed -n '/"path": "pioneer\/libxdjmod.so"/,/sha256/s/.*"sha256": "\([0-9a-f]*\)".*/\1/p' "$tmp/manifest.json")
got=$(sha256sum "$tmp/files/pioneer/pioneer/libxdjmod.so" | cut -d' ' -f1)
[ -n "$want" ] && [ "$want" = "$got" ] || { echo "sha256 mismatch: $got != $want" >&2; exit 1; }
install -m755 "$tmp/files/pioneer/pioneer/libxdjmod.so" "$out/libxdjmod.so"
echo "wrote $out/libxdjmod.so ($got)"
