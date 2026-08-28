#!/usr/bin/env python3
"""Disable Chromium's unsupported modern thread-priority policy on W2K."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


INPUT_SHA256 = "0173C32E5A4D975B8B1E6240EDA2E3B66738BC17EFD5310037191F9C429DE1D4"
SET_THREAD_PRIORITY_RVA = 0x00A5E510
EXPECTED_PROLOGUE = bytes.fromhex("55 89 E5 57 56 83 EC 08")
PATCH = bytes.fromhex("C3 90 90 90 90 90 90 90")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("chrome", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.chrome.read_bytes()
    if digest(before) != INPUT_SHA256:
        raise SystemExit("input is not the validated Candidate 12 chrome.dll")
    pe = pefile.PE(data=before, fast_load=True)
    function_offset = pe.get_offset_from_rva(SET_THREAD_PRIORITY_RVA)
    if before[function_offset:function_offset + len(EXPECTED_PROLOGUE)] != EXPECTED_PROLOGUE:
        raise SystemExit("unexpected SetThreadPriority prologue")

    patched = bytearray(before)
    patched[function_offset:function_offset + len(PATCH)] = PATCH
    checksum_offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", patched, checksum_offset, 0)
    checksum = pefile.PE(data=bytes(patched), fast_load=True).generate_checksum()
    struct.pack_into("<I", patched, checksum_offset, checksum)
    args.chrome.write_bytes(patched)

    result = {
        "candidate": 13,
        "purpose": (
            "avoid Windows 2000 renderer startup stalls in Chromium's modern "
            "thread-priority policy"
        ),
        "symbol": "base::(anonymous namespace)::SetThreadPriority",
        "rva": f"0x{SET_THREAD_PRIORITY_RVA:08X}",
        "replacement": "return immediately; retain Windows 2000 scheduler defaults",
        "checksum": f"0x{checksum:08X}",
        "sha256_before": digest(before),
        "sha256_after": digest(patched),
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
