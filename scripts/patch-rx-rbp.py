#!/usr/bin/env python3
"""Characterize and guard the native XDJ-RX `rbp` binary path.

This script is intentionally conservative:

- It verifies the input is a 32-bit little-endian ARM ELF.
- It computes and reports binary identity (size + md5 + sha256).
- It only applies byte patches when the binary fingerprint is explicitly known.

Unknown binaries are never patched. They can still be copied through unchanged so
the bring-up flow can continue while addresses are being characterized.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
from typing import Dict, List, Tuple


# Fingerprint map for known XDJ-RX binaries.
# key = md5 hex, value = metadata used for reporting and guarded patch selection.
KNOWN_RX_RBP: Dict[str, Dict[str, str]] = {}

# Patch table per known md5.
# Each tuple: (file_offset, expected_word, patched_word, note)
PATCH_TABLES: Dict[str, List[Tuple[int, int, int, str]]] = {}


def _hashes(path: str) -> Tuple[str, str, int]:
    md5 = hashlib.md5()
    sha = hashlib.sha256()
    size = 0
    with open(path, "rb") as f:
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            size += len(chunk)
            md5.update(chunk)
            sha.update(chunk)
    return md5.hexdigest(), sha.hexdigest(), size


def _read_elf_header(path: str) -> bytes:
    with open(path, "rb") as f:
        hdr = f.read(64)
    if len(hdr) < 64:
        raise SystemExit(f"{path}: file is too short to be ELF")
    return hdr


def _validate_elf(path: str) -> Dict[str, object]:
    hdr = _read_elf_header(path)

    if hdr[:4] != b"\x7fELF":
        raise SystemExit(f"{path}: not an ELF file")

    ei_class = hdr[4]
    ei_data = hdr[5]
    if ei_class != 1:
        raise SystemExit(f"{path}: expected ELFCLASS32, found {ei_class}")
    if ei_data != 1:
        raise SystemExit(f"{path}: expected little-endian ELF, found {ei_data}")

    e_type, e_machine = struct.unpack_from("<HH", hdr, 16)
    e_entry = struct.unpack_from("<I", hdr, 24)[0]
    if e_machine != 40:  # EM_ARM
        raise SystemExit(f"{path}: expected EM_ARM (40), found {e_machine}")
    if e_type not in (2, 3):  # ET_EXEC or ET_DYN
        raise SystemExit(f"{path}: expected ET_EXEC/ET_DYN, found {e_type}")

    return {
        "elf_class": "ELF32",
        "elf_endian": "little",
        "elf_type": e_type,
        "elf_machine": e_machine,
        "entry": e_entry,
    }


def _apply_guarded_patches(data: bytearray, table: List[Tuple[int, int, int, str]]) -> int:
    applied = 0
    for off, expected, patched, note in table:
        if off < 0 or off + 4 > len(data):
            raise SystemExit(f"offset {off:#x} is outside file (while applying: {note})")
        cur = struct.unpack_from("<I", data, off)[0]
        if cur == patched:
            continue
        if cur != expected:
            raise SystemExit(
                f"offset {off:#x}: expected {expected:#010x} or {patched:#010x}, "
                f"found {cur:#010x} (while applying: {note})"
            )
        struct.pack_into("<I", data, off, patched)
        applied += 1
    return applied


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input", help="input rbp path")
    ap.add_argument("-o", "--output", required=True, help="output rbp path")
    ap.add_argument(
        "--require-known",
        action="store_true",
        help="fail if input md5 is not in the known RX fingerprint table",
    )
    ap.add_argument(
        "--metadata-out",
        help="write characterization metadata JSON to this path",
    )
    args = ap.parse_args()

    elf = _validate_elf(args.input)
    md5, sha256, size = _hashes(args.input)
    known = KNOWN_RX_RBP.get(md5)

    if args.require_known and known is None:
        raise SystemExit(
            f"{args.input}: unknown RX rbp md5 {md5}; refusing because --require-known was set"
        )

    with open(args.input, "rb") as f:
        data = bytearray(f.read())

    applied = 0
    if known is not None:
        table = PATCH_TABLES.get(md5, [])
        if table:
            applied = _apply_guarded_patches(data, table)

    out_dir = os.path.dirname(args.output)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(data)

    out_md5 = hashlib.md5(data).hexdigest()
    out_sha256 = hashlib.sha256(data).hexdigest()

    meta = {
        "input": args.input,
        "output": args.output,
        "size_bytes": size,
        "input_md5": md5,
        "input_sha256": sha256,
        "known_signature": known,
        "patch_words_applied": applied,
        "output_md5": out_md5,
        "output_sha256": out_sha256,
        "elf": elf,
    }

    if args.metadata_out:
        meta_dir = os.path.dirname(args.metadata_out)
        if meta_dir:
            os.makedirs(meta_dir, exist_ok=True)
        with open(args.metadata_out, "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=2, sort_keys=True)
            f.write("\n")

    status = "known" if known is not None else "unknown"
    print(
        f"rx-rbp: {status} signature, size={size}, md5={md5}, "
        f"patch_words_applied={applied}"
    )
    if known is None:
        print("rx-rbp: unknown signatures are copied unchanged; no patch words were applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
