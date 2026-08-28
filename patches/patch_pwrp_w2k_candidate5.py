#!/usr/bin/env python3
"""Candidate-5 pwrp_k32 VEH bridge integration for Windows 2000.

Replaces candidate 4's temporary Add/RemoveVectoredExceptionHandler tokens
with position-independent lazy resolvers for the native w2kveh.dll bridge.
The candidate-4 GetProcessId implementation and earlier compatibility fixes
remain unchanged.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


STUB_LENGTH = 0x18
ADD_VEH_RVA = 0x1C360
REMOVE_VEH_RVA = 0x24FC0
ADD_BRIDGE_RVA = 0x2C900
REMOVE_BRIDGE_RVA = 0x2C940
STRINGS_RVA = 0x2C980
TEXT_RAW_END_RVA = 0x2CA00
LOAD_LIBRARY_A_IAT_RVA = 0x2D10C
GET_PROC_ADDRESS_IAT_RVA = 0x2D0B8

EXPECTED_ADD = bytes.fromhex(
    "8B 44 24 08 C2 08 00 " + "90 " * 17
)
EXPECTED_REMOVE = bytes.fromhex(
    "31 C0 83 7C 24 04 00 0F 95 C0 C2 04 00 " + "90 " * 11
)

DLL_NAME = b"w2kveh.dll\0"
ADD_NAME = b"_W2KAddVectoredExceptionHandler@8\0"
REMOVE_NAME = b"_W2KRemoveVectoredExceptionHandler@4\0"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def padded(code: bytes) -> bytes:
    if len(code) > STUB_LENGTH:
        raise ValueError("replacement exceeds wrapper stub")
    return code + b"\x90" * (STUB_LENGTH - len(code))


def relative_branch(opcode: int, source_rva: int, target_rva: int) -> bytes:
    return bytes([opcode]) + struct.pack("<i", target_rva - (source_rva + 5))


def lazy_resolver(
    bridge_rva: int,
    function_name_rva: int,
    failure_stack_bytes: int,
) -> bytes:
    """Build a relocatable tail-call resolver preserving stdcall arguments."""
    code = bytearray()
    code += b"\x53"                       # push ebx
    code += b"\xE8\x00\x00\x00\x00" # call next instruction
    code += b"\x5B"                       # pop ebx (runtime EIP)
    code += b"\x81\xEB" + struct.pack("<I", bridge_rva + 6)
    code += b"\x8D\x83" + struct.pack("<I", STRINGS_RVA)
    code += b"\x50"                       # push DLL name
    code += b"\xFF\x93" + struct.pack("<I", LOAD_LIBRARY_A_IAT_RVA)
    code += b"\x85\xC0"                   # test module, module
    first_failure_jump = len(code)
    code += b"\x74\x00"                   # jz failure
    code += b"\x8D\x8B" + struct.pack("<I", function_name_rva)
    code += b"\x51\x50"                   # push name; push module
    code += b"\xFF\x93" + struct.pack("<I", GET_PROC_ADDRESS_IAT_RVA)
    code += b"\x85\xC0"                   # test proc, proc
    second_failure_jump = len(code)
    code += b"\x74\x00"                   # jz failure
    code += b"\x5B\xFF\xE0"             # pop ebx; jmp eax
    failure = len(code)
    code += b"\x5B\x31\xC0\xC2" + struct.pack("<H", failure_stack_bytes)

    for jump in (first_failure_jump, second_failure_jump):
        displacement = failure - (jump + 2)
        if not -128 <= displacement <= 127:
            raise ValueError("short failure branch is out of range")
        code[jump + 1] = displacement & 0xFF
    return bytes(code)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.dll.read_bytes()
    pe = pefile.PE(data=before, fast_load=False)
    if digest(before) != "D80EB172E625193EEA6E9F6D73A33A5B452E8565ABDB5AD2F53FAA6857FD32B8":
        raise SystemExit("input is not the validated candidate-4 pwrp_k32.dll")

    add_offset = pe.get_offset_from_rva(ADD_VEH_RVA)
    remove_offset = pe.get_offset_from_rva(REMOVE_VEH_RVA)
    if before[add_offset : add_offset + STUB_LENGTH] != EXPECTED_ADD:
        raise SystemExit("unexpected candidate-4 AddVectoredExceptionHandler bytes")
    if before[remove_offset : remove_offset + STUB_LENGTH] != EXPECTED_REMOVE:
        raise SystemExit("unexpected candidate-4 RemoveVectoredExceptionHandler bytes")

    add_name_rva = STRINGS_RVA + len(DLL_NAME)
    remove_name_rva = add_name_rva + len(ADD_NAME)
    strings = DLL_NAME + ADD_NAME + REMOVE_NAME
    add_bridge = lazy_resolver(ADD_BRIDGE_RVA, add_name_rva, 8)
    remove_bridge = lazy_resolver(REMOVE_BRIDGE_RVA, remove_name_rva, 4)

    if ADD_BRIDGE_RVA + len(add_bridge) > REMOVE_BRIDGE_RVA:
        raise SystemExit("Add VEH resolver overlaps Remove VEH resolver")
    if REMOVE_BRIDGE_RVA + len(remove_bridge) > STRINGS_RVA:
        raise SystemExit("Remove VEH resolver overlaps strings")
    if STRINGS_RVA + len(strings) > TEXT_RAW_END_RVA:
        raise SystemExit("candidate-5 payload exceeds executable raw padding")

    for rva, length in (
        (ADD_BRIDGE_RVA, len(add_bridge)),
        (REMOVE_BRIDGE_RVA, len(remove_bridge)),
        (STRINGS_RVA, len(strings)),
    ):
        offset = pe.get_offset_from_rva(rva)
        if before[offset : offset + length] != b"\0" * length:
            raise SystemExit(f"payload region at RVA 0x{rva:X} is not empty")

    replacements = {
        ADD_VEH_RVA: padded(relative_branch(0xE9, ADD_VEH_RVA, ADD_BRIDGE_RVA)),
        REMOVE_VEH_RVA: padded(
            relative_branch(0xE9, REMOVE_VEH_RVA, REMOVE_BRIDGE_RVA)
        ),
        ADD_BRIDGE_RVA: add_bridge,
        REMOVE_BRIDGE_RVA: remove_bridge,
        STRINGS_RVA: strings,
    }

    patched = bytearray(before)
    records = []
    for rva, replacement in replacements.items():
        offset = pe.get_offset_from_rva(rva)
        original = before[offset : offset + len(replacement)]
        patched[offset : offset + len(replacement)] = replacement
        records.append(
            {
                "rva": f"0x{rva:X}",
                "file_offset": f"0x{offset:X}",
                "length": len(replacement),
                "before_bytes": original.hex(" ").upper(),
                "after_bytes": replacement.hex(" ").upper(),
            }
        )

    args.dll.write_bytes(patched)
    result = {
        "path": str(args.dll),
        "candidate": 5,
        "implemented": [
            "AddVectoredExceptionHandler via w2kveh.dll native dispatcher hook",
            "RemoveVectoredExceptionHandler with ordered, reference-counted registrations",
        ],
        "bridge": {
            "dll": DLL_NAME.rstrip(b"\0").decode("ascii"),
            "add_export": ADD_NAME.rstrip(b"\0").decode("ascii"),
            "remove_export": REMOVE_NAME.rstrip(b"\0").decode("ascii"),
        },
        "sha256_before": digest(before),
        "sha256_after": digest(bytes(patched)),
        "patches": records,
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
