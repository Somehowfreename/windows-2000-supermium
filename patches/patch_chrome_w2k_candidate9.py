#!/usr/bin/env python3
"""Route Chrome's WTS delay imports through the Windows 2000 wrapper."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


INPUT_SHA256 = "8153C0D90C46E3C12F1BC51C9F9A89449783563EC3B9733F1CD1168F2B90B4B7"
DELAY_DLL_NAME_RVA = 0xBF99F24
EXPECTED_NAME = b"WTSAPI32.dll\0"
REPLACEMENT_NAME = b"p_wtsapi.dll\0"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.dll.read_bytes()
    if digest(before) != INPUT_SHA256:
        raise SystemExit("input is not the validated candidate-8 chrome.dll")
    if len(EXPECTED_NAME) != len(REPLACEMENT_NAME):
        raise SystemExit("delay-import DLL names must have identical lengths")

    pe = pefile.PE(data=before, fast_load=True)
    name_offset = pe.get_offset_from_rva(DELAY_DLL_NAME_RVA)
    actual = before[name_offset : name_offset + len(EXPECTED_NAME)]
    if actual != EXPECTED_NAME:
        raise SystemExit(
            f"unexpected delay-import DLL name at RVA 0x{DELAY_DLL_NAME_RVA:X}: {actual!r}"
        )

    patched = bytearray(before)
    patched[name_offset : name_offset + len(REPLACEMENT_NAME)] = REPLACEMENT_NAME
    checksum_offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", patched, checksum_offset, 0)
    checksum = pefile.PE(data=bytes(patched), fast_load=True).generate_checksum()
    struct.pack_into("<I", patched, checksum_offset, checksum)
    args.dll.write_bytes(patched)

    result = {
        "path": str(args.dll),
        "candidate": 9,
        "implemented": [
            "route WTSAPI32 delay imports through p_wtsapi.dll",
            "forward WTSQuerySessionInformationW and WTSFreeMemory to Windows 2000",
            "emulate unavailable WTS session notification registration as successful",
        ],
        "delay_dll_name_rva": f"0x{DELAY_DLL_NAME_RVA:X}",
        "delay_dll_name_file_offset": f"0x{name_offset:X}",
        "delay_dll_before": EXPECTED_NAME[:-1].decode("ascii"),
        "delay_dll_after": REPLACEMENT_NAME[:-1].decode("ascii"),
        "checksum": f"0x{checksum:08X}",
        "sha256_before": digest(before),
        "sha256_after": digest(bytes(patched)),
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
