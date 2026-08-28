#!/usr/bin/env python3
"""Candidate-8 Unicode GetModuleHandleExW implementation for Windows 2000."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


INPUT_SHA256 = "03C139C4F959912AFCD67168B6EBDEE3072FE152397D0681830F6E2D1106A2CE"
EXPORT_RVA = 0x1A796
EXPECTED_EXPORT = bytes.fromhex("FF 25 F8 B0 06 10")
EXPORT_RELOCATION_RVA = 0x1A798
W2K_SECTION_RVA = 0x73000
W2K_SECTION_SIZE = 0x2000
RESOLVER_RVA = 0x73200
STRINGS_RVA = 0x73300
LOAD_LIBRARY_A_IAT_RVA = 0x2D10C
GET_PROC_ADDRESS_IAT_RVA = 0x2D0B8
DLL_NAME = b"w2kveh.dll\0"
FUNCTION_NAME = b"_W2KGetModuleHandleExW@12\0"
IMAGE_REL_BASED_ABSOLUTE = 0
IMAGE_REL_BASED_HIGHLOW = 3


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def relative_branch(opcode: int, source_rva: int, target_rva: int) -> bytes:
    return bytes([opcode]) + struct.pack("<i", target_rva - (source_rva + 5))


def lazy_resolver(function_name_rva: int, failure_stack_bytes: int) -> bytes:
    code = bytearray()
    code += b"\x53\xE8\x00\x00\x00\x00\x5B"
    code += b"\x81\xEB" + struct.pack("<I", RESOLVER_RVA + 6)
    code += b"\x8D\x83" + struct.pack("<I", STRINGS_RVA)
    code += b"\x50\xFF\x93" + struct.pack("<I", LOAD_LIBRARY_A_IAT_RVA)
    code += b"\x85\xC0"
    first_failure = len(code)
    code += b"\x74\x00"
    code += b"\x8D\x8B" + struct.pack("<I", function_name_rva)
    code += b"\x51\x50\xFF\x93" + struct.pack("<I", GET_PROC_ADDRESS_IAT_RVA)
    code += b"\x85\xC0"
    second_failure = len(code)
    code += b"\x74\x00\x5B\xFF\xE0"
    failure = len(code)
    code += b"\x5B\x31\xC0\xC2" + struct.pack("<H", failure_stack_bytes)
    for jump in (first_failure, second_failure):
        code[jump + 1] = (failure - (jump + 2)) & 0xFF
    return bytes(code)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.dll.read_bytes()
    pe = pefile.PE(data=before, fast_load=False)
    if digest(before) != INPUT_SHA256:
        raise SystemExit("input is not the validated candidate-7 pwrp_k32.dll")
    section = next((s for s in pe.sections if s.Name.rstrip(b"\0") == b".w2k"), None)
    if section is None or section.VirtualAddress != W2K_SECTION_RVA or section.SizeOfRawData != W2K_SECTION_SIZE:
        raise SystemExit("validated .w2k patch section is missing")

    export_offset = pe.get_offset_from_rva(EXPORT_RVA)
    if before[export_offset : export_offset + 6] != EXPECTED_EXPORT:
        raise SystemExit("unexpected GetModuleHandleExW export trampoline")
    relocation = next(
        (
            entry
            for block in pe.DIRECTORY_ENTRY_BASERELOC
            for entry in block.entries
            if entry.rva == EXPORT_RELOCATION_RVA
        ),
        None,
    )
    if relocation is None or relocation.type != IMAGE_REL_BASED_HIGHLOW:
        raise SystemExit("GetModuleHandleExW export relocation is missing")

    strings = DLL_NAME + FUNCTION_NAME
    resolver = lazy_resolver(STRINGS_RVA + len(DLL_NAME), 12)
    resolver_offset = pe.get_offset_from_rva(RESOLVER_RVA)
    strings_offset = pe.get_offset_from_rva(STRINGS_RVA)
    if before[resolver_offset : resolver_offset + len(resolver)] != b"\0" * len(resolver):
        raise SystemExit("candidate-8 resolver region is not empty")
    if before[strings_offset : strings_offset + len(strings)] != b"\0" * len(strings):
        raise SystemExit("candidate-8 string region is not empty")

    patched = bytearray(before)
    export_patch = relative_branch(0xE9, EXPORT_RVA, RESOLVER_RVA) + b"\x90"
    patched[export_offset : export_offset + 6] = export_patch
    patched[resolver_offset : resolver_offset + len(resolver)] = resolver
    patched[strings_offset : strings_offset + len(strings)] = strings

    relocation_offset = relocation.struct.get_file_offset()
    relocation_word = struct.unpack_from("<H", before, relocation_offset)[0]
    struct.pack_into("<H", patched, relocation_offset, relocation_word & 0x0FFF)
    checksum_offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", patched, checksum_offset, 0)
    checksum = pefile.PE(data=bytes(patched), fast_load=True).generate_checksum()
    struct.pack_into("<I", patched, checksum_offset, checksum)

    args.dll.write_bytes(patched)
    result = {
        "path": str(args.dll),
        "candidate": 8,
        "implemented": [
            "GetModuleHandleExW with correct output, result, and stdcall cleanup",
            "FROM_ADDRESS and UNCHANGED_REFCOUNT semantics shared with ANSI bridge",
        ],
        "export_rva": f"0x{EXPORT_RVA:X}",
        "resolver_rva": f"0x{RESOLVER_RVA:X}",
        "relocation": {
            "rva": f"0x{EXPORT_RELOCATION_RVA:X}",
            "file_offset": f"0x{relocation_offset:X}",
            "before_type": IMAGE_REL_BASED_HIGHLOW,
            "after_type": IMAGE_REL_BASED_ABSOLUTE,
        },
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
