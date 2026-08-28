#!/usr/bin/env python3
"""Candidate-4 Windows 2000 pwrp_k32 startup compatibility patches.

GetProcessId is implemented with Win2000's NtQueryInformationProcess.
Add/RemoveVectoredExceptionHandler receive a temporary registration-token shim
to advance startup discovery.  The VEH shim intentionally does not dispatch
exceptions and must be replaced by a real process-wide implementation before
the final release.
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
GET_PROCESS_ID_RVA = 0x215E0
REMOVE_VEH_RVA = 0x24FC0
CODE_CAVE_RVA = 0x2C8C0
NT_QUERY_INFORMATION_PROCESS_THUNK_RVA = 0x2C8A2

EXPECTED = {
    ADD_VEH_RVA: bytes.fromhex(
        "6A 00 68 E8 64 03 10 68 A0 67 03 10 6A 00 "
        "E8 47 05 01 00 E9 3C 05 01 00"
    ),
    GET_PROCESS_ID_RVA: bytes.fromhex(
        "6A 00 68 E8 64 03 10 68 5C CA 03 10 6A 00 "
        "E8 C7 B2 00 00 E9 BC B2 00 00"
    ),
    REMOVE_VEH_RVA: bytes.fromhex(
        "6A 00 68 E8 64 03 10 68 C8 0A 04 10 6A 00 "
        "E8 E7 78 00 00 E9 DC 78 00 00"
    ),
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def padded(code: bytes) -> bytes:
    if len(code) > STUB_LENGTH:
        raise ValueError("replacement exceeds stub")
    return code + b"\x90" * (STUB_LENGTH - len(code))


def relative_branch(opcode: int, source_rva: int, target_rva: int) -> bytes:
    return bytes([opcode]) + struct.pack("<i", target_rva - (source_rva + 5))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.dll.read_bytes()
    pe = pefile.PE(data=before, fast_load=False)
    exports = {
        symbol.name.decode("ascii"): symbol.address
        for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols
        if symbol.name
    }
    expected_exports = {
        "AddVectoredExceptionHandler": 0x1985D,
        "GetProcessId": 0x1A922,
        "RemoveVectoredExceptionHandler": 0x1B5A0,
    }
    for name, rva in expected_exports.items():
        if exports.get(name) != rva:
            raise SystemExit(f"unexpected {name} export RVA")

    for rva, expected in EXPECTED.items():
        offset = pe.get_offset_from_rva(rva)
        if before[offset : offset + STUB_LENGTH] != expected:
            raise SystemExit(f"unexpected original bytes at RVA 0x{rva:X}")

    cave_offset = pe.get_offset_from_rva(CODE_CAVE_RVA)
    if before[cave_offset : cave_offset + 0x60] != b"\x00" * 0x60:
        raise SystemExit("candidate-4 code cave is not empty")

    # Temporary token semantics: a non-null handler is its own registration.
    add_veh = padded(bytes.fromhex("8B 44 24 08 C2 08 00"))
    remove_veh = padded(bytes.fromhex("31 C0 83 7C 24 04 00 0F 95 C0 C2 04 00"))

    # DWORD GetProcessId(HANDLE): query ProcessBasicInformation (class 0),
    # whose UniqueProcessId is the fifth 32-bit field on Win2000 x86.
    get_process_id = bytearray()
    get_process_id += bytes.fromhex("83 EC 18")       # sub esp, 24
    get_process_id += bytes.fromhex("89 E0")          # mov eax, esp
    get_process_id += bytes.fromhex("6A 00")          # ReturnLength = NULL
    get_process_id += bytes.fromhex("6A 18")          # structure length
    get_process_id += bytes.fromhex("50")             # structure pointer
    get_process_id += bytes.fromhex("6A 00")          # ProcessBasicInformation
    get_process_id += bytes.fromhex("FF 74 24 2C")    # original process handle
    call_rva = CODE_CAVE_RVA + len(get_process_id)
    get_process_id += relative_branch(
        0xE8, call_rva, NT_QUERY_INFORMATION_PROCESS_THUNK_RVA
    )
    get_process_id += bytes.fromhex("85 C0 78 0A")     # NT failure -> zero
    get_process_id += bytes.fromhex("8B 44 24 10")    # UniqueProcessId
    get_process_id += bytes.fromhex("83 C4 18 C2 04 00")
    get_process_id += bytes.fromhex("31 C0 83 C4 18 C2 04 00")

    patched = bytearray(before)
    patches = {
        ADD_VEH_RVA: add_veh,
        GET_PROCESS_ID_RVA: padded(
            relative_branch(0xE9, GET_PROCESS_ID_RVA, CODE_CAVE_RVA)
        ),
        REMOVE_VEH_RVA: remove_veh,
        CODE_CAVE_RVA: bytes(get_process_id),
    }
    records = []
    for rva, replacement in patches.items():
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
        "candidate": 4,
        "implemented": [
            "GetProcessId via NtQueryInformationProcess(ProcessBasicInformation)",
            "AddVectoredExceptionHandler temporary non-null registration token",
            "RemoveVectoredExceptionHandler temporary token validation",
        ],
        "temporary_limitations": [
            "Vectored exception handlers are registered symbolically but are not dispatched; replace before release"
        ],
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
