#!/usr/bin/env python3
"""Candidate-6 Windows 2000 pwrp_k32 startup compatibility patches.

Implements Win2K's unavoidable AttachConsole limitation as a standards-shaped
failure (FALSE with ERROR_CALL_NOT_IMPLEMENTED), and cleans stale HIGHLOW
relocations from every wrapper stub replaced by position-independent code.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


STUB_LENGTH = 0x18
ATTACH_CONSOLE_RVA = 0x1C5C0
ATTACH_BRIDGE_RVA = 0x2C9E0
TEXT_RAW_END_RVA = 0x2CA00
SET_LAST_ERROR_IAT_RVA = 0x2D054
ERROR_CALL_NOT_IMPLEMENTED = 120

EXPECTED_ATTACH = bytes.fromhex(
    "6A 00 68 E8 64 03 10 68 E0 6A 03 10 6A 00 "
    "E8 E7 02 01 00 E9 DC 02 01 00"
)

# The first relocation of GetNativeSystemInfo deliberately relocates its
# GetSystemInfo IAT operand. Every other entry below points into bytes replaced
# with relative or position-independent code and must become ABSOLUTE padding.
STALE_HIGHLOW_RVAS = (
    0x1C363,
    0x1C368,
    0x1C5C3,
    0x1C5C8,
    0x21028,
    0x215E3,
    0x215E8,
    0x24FC3,
    0x24FC8,
)

IMAGE_REL_BASED_ABSOLUTE = 0
IMAGE_REL_BASED_HIGHLOW = 3


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def padded(code: bytes) -> bytes:
    if len(code) > STUB_LENGTH:
        raise ValueError("replacement exceeds wrapper stub")
    return code + b"\x90" * (STUB_LENGTH - len(code))


def relative_branch(opcode: int, source_rva: int, target_rva: int) -> bytes:
    return bytes([opcode]) + struct.pack("<i", target_rva - (source_rva + 5))


def build_attach_bridge() -> bytes:
    code = bytearray()
    code += b"\x53"                       # push ebx
    code += b"\xE8\x00\x00\x00\x00" # call next instruction
    code += b"\x5B"                       # pop ebx (runtime EIP)
    code += b"\x81\xEB" + struct.pack("<I", ATTACH_BRIDGE_RVA + 6)
    code += b"\x6A" + bytes([ERROR_CALL_NOT_IMPLEMENTED])
    code += b"\xFF\x93" + struct.pack("<I", SET_LAST_ERROR_IAT_RVA)
    code += b"\x5B"                       # restore callee-saved EBX
    code += b"\x31\xC0"                   # FALSE
    code += b"\xC2\x04\x00"             # stdcall return, one argument
    return bytes(code)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.dll.read_bytes()
    pe = pefile.PE(data=before, fast_load=False)
    if digest(before) != "2591AFBE91363C1B1B2DA04957F8F74ACAA75A34B96C7718F6A8C4995D0CD75B":
        raise SystemExit("input is not the validated candidate-5 pwrp_k32.dll")

    attach_offset = pe.get_offset_from_rva(ATTACH_CONSOLE_RVA)
    if before[attach_offset : attach_offset + STUB_LENGTH] != EXPECTED_ATTACH:
        raise SystemExit("unexpected AttachConsole stub bytes")

    bridge = build_attach_bridge()
    if ATTACH_BRIDGE_RVA + len(bridge) > TEXT_RAW_END_RVA:
        raise SystemExit("AttachConsole bridge exceeds executable raw padding")
    bridge_offset = pe.get_offset_from_rva(ATTACH_BRIDGE_RVA)
    if before[bridge_offset : bridge_offset + len(bridge)] != b"\0" * len(bridge):
        raise SystemExit("AttachConsole bridge region is not empty")

    relocation_by_rva = {
        entry.rva: entry
        for block in pe.DIRECTORY_ENTRY_BASERELOC
        for entry in block.entries
    }
    for rva in STALE_HIGHLOW_RVAS:
        entry = relocation_by_rva.get(rva)
        if entry is None or entry.type != IMAGE_REL_BASED_HIGHLOW:
            raise SystemExit(f"expected HIGHLOW relocation missing at RVA 0x{rva:X}")

    patched = bytearray(before)
    attach_patch = padded(
        relative_branch(0xE9, ATTACH_CONSOLE_RVA, ATTACH_BRIDGE_RVA)
    )
    patched[attach_offset : attach_offset + STUB_LENGTH] = attach_patch
    patched[bridge_offset : bridge_offset + len(bridge)] = bridge

    relocation_records = []
    for rva in STALE_HIGHLOW_RVAS:
        entry = relocation_by_rva[rva]
        offset = entry.struct.get_file_offset()
        word = struct.unpack_from("<H", before, offset)[0]
        struct.pack_into("<H", patched, offset, word & 0x0FFF)
        relocation_records.append(
            {
                "rva": f"0x{rva:X}",
                "file_offset": f"0x{offset:X}",
                "before_type": IMAGE_REL_BASED_HIGHLOW,
                "after_type": IMAGE_REL_BASED_ABSOLUTE,
            }
        )

    args.dll.write_bytes(patched)
    result = {
        "path": str(args.dll),
        "candidate": 6,
        "implemented": [
            "AttachConsole returns FALSE and sets ERROR_CALL_NOT_IMPLEMENTED on Windows 2000",
            "Stale relocations neutralized for all position-independent patched wrapper stubs",
        ],
        "attach_console": {
            "stub_rva": f"0x{ATTACH_CONSOLE_RVA:X}",
            "bridge_rva": f"0x{ATTACH_BRIDGE_RVA:X}",
            "error": ERROR_CALL_NOT_IMPLEMENTED,
            "before_bytes": EXPECTED_ATTACH.hex(" ").upper(),
            "after_bytes": attach_patch.hex(" ").upper(),
            "bridge_bytes": bridge.hex(" ").upper(),
        },
        "relocations": relocation_records,
        "sha256_before": digest(before),
        "sha256_after": digest(bytes(patched)),
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
