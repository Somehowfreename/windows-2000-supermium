#!/usr/bin/env python3
"""Candidate-7 GetModuleHandleExA implementation and patch section.

Adds an executable `.w2k` section for scalable compatibility thunks and routes
the public GetModuleHandleExA export through the validated w2kveh.dll bridge.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


INPUT_SHA256 = "694402D5B2A59D47B92ADB3ADACB6363627F614A62F54090339207E4ABD1A518"
EXPORT_RVA = 0x1A790
EXPECTED_EXPORT = bytes.fromhex("FF 25 FC B0 06 10")
EXPORT_RELOCATION_RVA = 0x1A792

SECTION_NAME = b".w2k\0\0\0\0"
SECTION_RVA = 0x73000
SECTION_VIRTUAL_SIZE = 0x2000
SECTION_RAW_SIZE = 0x2000
SECTION_CHARACTERISTICS = 0x60000020
RESOLVER_RVA = SECTION_RVA
STRINGS_RVA = SECTION_RVA + 0x100

LOAD_LIBRARY_A_IAT_RVA = 0x2D10C
GET_PROC_ADDRESS_IAT_RVA = 0x2D0B8
DLL_NAME = b"w2kveh.dll\0"
FUNCTION_NAME = b"_W2KGetModuleHandleExA@12\0"

IMAGE_REL_BASED_ABSOLUTE = 0
IMAGE_REL_BASED_HIGHLOW = 3


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def relative_branch(opcode: int, source_rva: int, target_rva: int) -> bytes:
    return bytes([opcode]) + struct.pack("<i", target_rva - (source_rva + 5))


def lazy_resolver(function_name_rva: int, failure_stack_bytes: int) -> bytes:
    code = bytearray()
    code += b"\x53"                       # push ebx
    code += b"\xE8\x00\x00\x00\x00" # call next instruction
    code += b"\x5B"                       # pop ebx (runtime EIP)
    code += b"\x81\xEB" + struct.pack("<I", RESOLVER_RVA + 6)
    code += b"\x8D\x83" + struct.pack("<I", STRINGS_RVA)
    code += b"\x50"
    code += b"\xFF\x93" + struct.pack("<I", LOAD_LIBRARY_A_IAT_RVA)
    code += b"\x85\xC0"
    first_failure_jump = len(code)
    code += b"\x74\x00"
    code += b"\x8D\x8B" + struct.pack("<I", function_name_rva)
    code += b"\x51\x50"
    code += b"\xFF\x93" + struct.pack("<I", GET_PROC_ADDRESS_IAT_RVA)
    code += b"\x85\xC0"
    second_failure_jump = len(code)
    code += b"\x74\x00"
    code += b"\x5B\xFF\xE0"
    failure = len(code)
    code += b"\x5B\x31\xC0\xC2" + struct.pack("<H", failure_stack_bytes)
    for jump in (first_failure_jump, second_failure_jump):
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
        raise SystemExit("input is not the validated candidate-6 pwrp_k32.dll")
    if pe.FILE_HEADER.NumberOfSections != 5:
        raise SystemExit("unexpected input section count")
    if pe.OPTIONAL_HEADER.FileAlignment != 0x200 or pe.OPTIONAL_HEADER.SectionAlignment != 0x1000:
        raise SystemExit("unexpected PE alignment")
    if pe.OPTIONAL_HEADER.SizeOfImage != SECTION_RVA:
        raise SystemExit("new section RVA no longer equals the input image end")

    last = pe.sections[-1]
    raw_end = last.PointerToRawData + last.SizeOfRawData
    if raw_end != len(before) or raw_end != align(raw_end, pe.OPTIONAL_HEADER.FileAlignment):
        raise SystemExit("input file does not end at an aligned final section")

    export_offset = pe.get_offset_from_rva(EXPORT_RVA)
    if before[export_offset : export_offset + len(EXPECTED_EXPORT)] != EXPECTED_EXPORT:
        raise SystemExit("unexpected GetModuleHandleExA export trampoline")

    export_relocation = None
    for block in pe.DIRECTORY_ENTRY_BASERELOC:
        for entry in block.entries:
            if entry.rva == EXPORT_RELOCATION_RVA:
                export_relocation = entry
                break
    if export_relocation is None or export_relocation.type != IMAGE_REL_BASED_HIGHLOW:
        raise SystemExit("GetModuleHandleExA export relocation is missing")

    first_section_header = pe.sections[0].get_file_offset()
    new_section_header = first_section_header + pe.FILE_HEADER.NumberOfSections * 40
    if before[new_section_header : new_section_header + 40] != b"\0" * 40:
        raise SystemExit("PE header has no empty section-header slot")
    if new_section_header + 40 > pe.OPTIONAL_HEADER.SizeOfHeaders:
        raise SystemExit("new section header exceeds SizeOfHeaders")

    strings = DLL_NAME + FUNCTION_NAME
    function_name_rva = STRINGS_RVA + len(DLL_NAME)
    resolver = lazy_resolver(function_name_rva, 12)
    section_data = bytearray(SECTION_RAW_SIZE)
    section_data[0 : len(resolver)] = resolver
    strings_offset = STRINGS_RVA - SECTION_RVA
    section_data[strings_offset : strings_offset + len(strings)] = strings

    patched = bytearray(before)
    patched += section_data
    export_patch = relative_branch(0xE9, EXPORT_RVA, RESOLVER_RVA) + b"\x90"
    patched[export_offset : export_offset + len(export_patch)] = export_patch

    relocation_offset = export_relocation.struct.get_file_offset()
    relocation_word = struct.unpack_from("<H", before, relocation_offset)[0]
    struct.pack_into("<H", patched, relocation_offset, relocation_word & 0x0FFF)

    section_header = struct.pack(
        "<8sIIIIIIHHI",
        SECTION_NAME,
        SECTION_VIRTUAL_SIZE,
        SECTION_RVA,
        SECTION_RAW_SIZE,
        raw_end,
        0,
        0,
        0,
        0,
        SECTION_CHARACTERISTICS,
    )
    patched[new_section_header : new_section_header + 40] = section_header

    number_sections_offset = pe.FILE_HEADER.get_field_absolute_offset("NumberOfSections")
    size_code_offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("SizeOfCode")
    size_image_offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("SizeOfImage")
    checksum_offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<H", patched, number_sections_offset, 6)
    struct.pack_into(
        "<I", patched, size_code_offset,
        pe.OPTIONAL_HEADER.SizeOfCode + SECTION_RAW_SIZE,
    )
    struct.pack_into(
        "<I", patched, size_image_offset,
        align(SECTION_RVA + SECTION_VIRTUAL_SIZE, pe.OPTIONAL_HEADER.SectionAlignment),
    )
    struct.pack_into("<I", patched, checksum_offset, 0)

    validation = pefile.PE(data=bytes(patched), fast_load=False)
    if validation.FILE_HEADER.NumberOfSections != 6 or validation.sections[-1].Name != SECTION_NAME:
        raise SystemExit("new section failed PE validation")
    checksum = validation.generate_checksum()
    struct.pack_into("<I", patched, checksum_offset, checksum)

    args.dll.write_bytes(patched)
    result = {
        "path": str(args.dll),
        "candidate": 7,
        "implemented": [
            "GetModuleHandleExA including FROM_ADDRESS and UNCHANGED_REFCOUNT",
            "new executable .w2k section for subsequent compatibility thunks",
        ],
        "export_rva": f"0x{EXPORT_RVA:X}",
        "resolver_rva": f"0x{RESOLVER_RVA:X}",
        "section": {
            "name": ".w2k",
            "rva": f"0x{SECTION_RVA:X}",
            "virtual_size": SECTION_VIRTUAL_SIZE,
            "raw_offset": f"0x{raw_end:X}",
            "raw_size": SECTION_RAW_SIZE,
            "characteristics": f"0x{SECTION_CHARACTERISTICS:08X}",
        },
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
