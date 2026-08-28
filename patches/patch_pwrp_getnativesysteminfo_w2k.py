#!/usr/bin/env python3
"""Replace pwrp_k32's GetNativeSystemInfo debug stub for 32-bit Windows 2000.

On a native 32-bit operating system GetNativeSystemInfo has the same observable
result as GetSystemInfo.  The replacement tail-jumps through the DLL's existing
GetSystemInfo IAT entry and deliberately places its absolute operand on an
existing IMAGE_REL_BASED_HIGHLOW relocation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import pefile


STUB_RVA = 0x21020
STUB_LENGTH = 0x18
RELOCATION_RVA = 0x21023
GET_SYSTEM_INFO_IAT_VA = 0x1002D140
GET_NATIVE_SYSTEM_INFO_EXPORT_RVA = 0x1A7EA
IMAGE_REL_BASED_HIGHLOW = 3

EXPECTED = bytes.fromhex(
    "6A 00 68 E8 64 03 10 68 54 C3 03 10 6A 00 "
    "E8 87 B8 00 00 E9 7C B8 00 00"
)
PATCH = bytes.fromhex("90 FF 25 40 D1 02 10") + b"\x90" * (STUB_LENGTH - 7)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.dll.read_bytes()
    pe = pefile.PE(data=before, fast_load=False)

    exports = {
        symbol.name.decode("ascii", errors="strict"): symbol.address
        for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols
        if symbol.name
    }
    if exports.get("GetNativeSystemInfo") != GET_NATIVE_SYSTEM_INFO_EXPORT_RVA:
        raise SystemExit("unexpected GetNativeSystemInfo export RVA")

    iat_entries = {
        (descriptor.dll.decode("ascii").lower(), entry.name.decode("ascii")): entry.address
        for descriptor in pe.DIRECTORY_ENTRY_IMPORT
        for entry in descriptor.imports
        if entry.name
    }
    if iat_entries.get(("kernel32.dll", "GetSystemInfo")) != GET_SYSTEM_INFO_IAT_VA:
        raise SystemExit("unexpected KERNEL32!GetSystemInfo IAT address")

    relocations = {
        (entry.rva, entry.type)
        for block in pe.DIRECTORY_ENTRY_BASERELOC
        for entry in block.entries
    }
    if (RELOCATION_RVA, IMAGE_REL_BASED_HIGHLOW) not in relocations:
        raise SystemExit("required HIGHLOW relocation is absent")

    offset = pe.get_offset_from_rva(STUB_RVA)
    actual = before[offset : offset + STUB_LENGTH]
    if actual != EXPECTED:
        raise SystemExit(
            f"unexpected stub bytes at RVA 0x{STUB_RVA:X}: {actual.hex(' ')}"
        )

    patched = bytearray(before)
    patched[offset : offset + STUB_LENGTH] = PATCH
    args.dll.write_bytes(patched)

    result = {
        "path": str(args.dll),
        "purpose": "Implement GetNativeSystemInfo on native 32-bit Windows 2000 by tail-jumping to GetSystemInfo",
        "rva": f"0x{STUB_RVA:X}",
        "file_offset": f"0x{offset:X}",
        "length": STUB_LENGTH,
        "relocation_rva": f"0x{RELOCATION_RVA:X}",
        "iat_target_va": f"0x{GET_SYSTEM_INFO_IAT_VA:X}",
        "before_bytes": EXPECTED.hex(" ").upper(),
        "after_bytes": PATCH.hex(" ").upper(),
        "sha256_before": sha256(before),
        "sha256_after": sha256(bytes(patched)),
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
