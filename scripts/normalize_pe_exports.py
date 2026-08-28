#!/usr/bin/env python3
"""Rewrite longer PE export names in place and refresh the checksum."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument(
        "mapping",
        nargs="+",
        help="old=new export-name mappings; the replacement may not be longer",
    )
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.image.read_bytes()
    patched = bytearray(before)
    changes: list[dict[str, object]] = []
    for item in args.mapping:
        old_text, separator, new_text = item.partition("=")
        if not separator or not old_text or not new_text:
            raise SystemExit(f"invalid mapping: {item!r}")
        old = old_text.encode("ascii") + b"\0"
        new = new_text.encode("ascii") + b"\0"
        if len(new) > len(old):
            raise SystemExit(f"replacement is longer: {item!r}")
        offsets = []
        start = 0
        while True:
            offset = before.find(old, start)
            if offset < 0:
                break
            offsets.append(offset)
            start = offset + 1
        if len(offsets) != 1:
            raise SystemExit(
                f"expected exactly one occurrence of {old_text!r}, found {len(offsets)}"
            )
        offset = offsets[0]
        patched[offset : offset + len(old)] = new + b"\0" * (len(old) - len(new))
        changes.append({"old": old_text, "new": new_text, "file_offset": offset})

    pe = pefile.PE(data=bytes(patched), fast_load=True)
    checksum_offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", patched, checksum_offset, 0)
    checksum = pefile.PE(data=bytes(patched), fast_load=True).generate_checksum()
    struct.pack_into("<I", patched, checksum_offset, checksum)
    args.image.write_bytes(patched)

    result = {
        "path": str(args.image),
        "changes": changes,
        "checksum": f"0x{checksum:08X}",
        "sha256_before": hashlib.sha256(before).hexdigest().upper(),
        "sha256_after": hashlib.sha256(patched).hexdigest().upper(),
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
