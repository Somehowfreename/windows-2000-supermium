#!/usr/bin/env python3
"""Route pwrp synchronization exports to the native W2K sync bridge."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


INPUT_SHA256 = "7161AB7E44178F6770D7A73338D4F81868AC86ACB972FBBFC782ABF17C76C688"
W2K_SECTION_RVA = 0x73000
W2K_SECTION_SIZE = 0x2000
RESOLVERS_RVA = 0x73400
RESOLVER_STRIDE = 0x50
SLOTS_RVA = 0x73900
STRINGS_RVA = 0x73A00
LOAD_LIBRARY_A_IAT_RVA = 0x2D10C
GET_PROC_ADDRESS_IAT_RVA = 0x2D0B8
DLL_NAME = b"w2ksync.dll\0"
IMAGE_REL_BASED_ABSOLUTE = 0
IMAGE_REL_BASED_HIGHLOW = 3

FUNCTIONS = [
    ("AcquireSRWLockExclusive", 0x197E5, b"_W2KAcquireSRWLockExclusive@4\0", 4),
    ("AcquireSRWLockShared", 0x197EB, b"_W2KAcquireSRWLockShared@4\0", 4),
    ("InitializeConditionVariable", 0x1AD2A, b"_W2KInitializeConditionVariable@4\0", 4),
    ("InitializeSRWLock", 0x1AD60, b"_W2KInitializeSRWLock@4\0", 4),
    ("ReleaseSRWLockExclusive", 0x1B552, b"_W2KReleaseSRWLockExclusive@4\0", 4),
    ("ReleaseSRWLockShared", 0x1B558, b"_W2KReleaseSRWLockShared@4\0", 4),
    ("SleepConditionVariableCS", 0x1BA80, b"_W2KSleepConditionVariableCS@12\0", 12),
    ("SleepConditionVariableSRW", 0x1BA86, b"_W2KSleepConditionVariableSRW@16\0", 16),
    ("TryAcquireSRWLockExclusive", 0x1BB76, b"_W2KTryAcquireSRWLockExclusive@4\0", 4),
    ("TryAcquireSRWLockShared", 0x1BB7C, b"_W2KTryAcquireSRWLockShared@4\0", 4),
    ("WakeAllConditionVariable", 0x1BCDE, b"_W2KWakeAllConditionVariable@4\0", 4),
    ("WakeConditionVariable", 0x1BCF0, b"_W2KWakeConditionVariable@4\0", 4),
]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def relative_branch(opcode: int, source_rva: int, target_rva: int) -> bytes:
    return bytes([opcode]) + struct.pack("<i", target_rva - (source_rva + 5))


def resolver(resolver_rva: int, slot_rva: int, function_name_rva: int,
             failure_stack_bytes: int) -> bytes:
    code = bytearray()
    code += b"\x53"                           # push ebx
    code += b"\xE8\x00\x00\x00\x00"         # call next instruction
    code += b"\x5B"                           # pop ebx
    code += b"\x81\xEB" + struct.pack("<I", resolver_rva + 6)
    code += b"\x8B\x83" + struct.pack("<I", slot_rva)
    code += b"\x85\xC0"
    cached_jump = len(code)
    code += b"\x75\x00"
    code += b"\x8D\x83" + struct.pack("<I", STRINGS_RVA)
    code += b"\x50"
    code += b"\xFF\x93" + struct.pack("<I", LOAD_LIBRARY_A_IAT_RVA)
    code += b"\x85\xC0"
    load_failure = len(code)
    code += b"\x74\x00"
    code += b"\x8D\x8B" + struct.pack("<I", function_name_rva)
    code += b"\x51\x50"
    code += b"\xFF\x93" + struct.pack("<I", GET_PROC_ADDRESS_IAT_RVA)
    code += b"\x85\xC0"
    proc_failure = len(code)
    code += b"\x74\x00"
    code += b"\x89\x83" + struct.pack("<I", slot_rva)
    success = len(code)
    code += b"\x5B\xFF\xE0"                 # pop ebx; jmp eax
    failure = len(code)
    code += b"\x5B\x31\xC0\xC2" + struct.pack("<H", failure_stack_bytes)
    for jump, target in ((cached_jump, success),
                         (load_failure, failure),
                         (proc_failure, failure)):
        displacement = target - (jump + 2)
        if not -128 <= displacement <= 127:
            raise ValueError("resolver short branch is out of range")
        code[jump + 1] = displacement & 0xFF
    if len(code) > RESOLVER_STRIDE:
        raise ValueError("resolver exceeds reserved stride")
    return bytes(code)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.dll.read_bytes()
    if digest(before) != INPUT_SHA256:
        raise SystemExit("input is not the validated candidate-10 pwrp_k32.dll")
    pe = pefile.PE(data=before, fast_load=False)
    section = next(
        (s for s in pe.sections if s.Name.rstrip(b"\0") == b".w2k"), None
    )
    if (section is None or section.VirtualAddress != W2K_SECTION_RVA or
            section.SizeOfRawData != W2K_SECTION_SIZE):
        raise SystemExit("validated .w2k patch section is missing")
    if section.Characteristics != 0x60000020:
        raise SystemExit("unexpected .w2k section characteristics")

    patched = bytearray(before)
    # Resolver cache slots live in the existing .w2k section.  Add write
    # permission so the first lookup can publish its resolved function pointer.
    section_characteristics_offset = section.get_file_offset() + 36
    struct.pack_into("<I", patched, section_characteristics_offset,
                     section.Characteristics | 0x80000000)
    names = bytearray(DLL_NAME)
    records = []
    relocations = {
        entry.rva: entry
        for block in pe.DIRECTORY_ENTRY_BASERELOC
        for entry in block.entries
    }

    resolver_area_end = RESOLVERS_RVA + len(FUNCTIONS) * RESOLVER_STRIDE
    strings_end = STRINGS_RVA + len(DLL_NAME) + sum(len(item[2]) for item in FUNCTIONS)
    for start, end, label in (
        (RESOLVERS_RVA, resolver_area_end, "resolver"),
        (SLOTS_RVA, SLOTS_RVA + len(FUNCTIONS) * 4, "cache slot"),
        (STRINGS_RVA, strings_end, "string"),
    ):
        offset = pe.get_offset_from_rva(start)
        if before[offset:offset + end - start] != b"\0" * (end - start):
            raise SystemExit(f"candidate-11 {label} area is not empty")

    for index, (name, export_rva, bridge_name, stack_bytes) in enumerate(FUNCTIONS):
        export_offset = pe.get_offset_from_rva(export_rva)
        if before[export_offset:export_offset + 2] != b"\xFF\x25":
            raise SystemExit(f"unexpected {name} export trampoline")
        relocation_rva = export_rva + 2
        relocation = relocations.get(relocation_rva)
        if relocation is None or relocation.type != IMAGE_REL_BASED_HIGHLOW:
            raise SystemExit(f"missing {name} export relocation")

        current_resolver_rva = RESOLVERS_RVA + index * RESOLVER_STRIDE
        slot_rva = SLOTS_RVA + index * 4
        function_name_rva = STRINGS_RVA + len(names)
        code = resolver(current_resolver_rva, slot_rva, function_name_rva,
                        stack_bytes)
        code_offset = pe.get_offset_from_rva(current_resolver_rva)
        patched[code_offset:code_offset + len(code)] = code
        names += bridge_name

        export_patch = relative_branch(0xE9, export_rva,
                                       current_resolver_rva) + b"\x90"
        patched[export_offset:export_offset + 6] = export_patch
        relocation_offset = relocation.struct.get_file_offset()
        word = struct.unpack_from("<H", before, relocation_offset)[0]
        struct.pack_into("<H", patched, relocation_offset, word & 0x0FFF)
        records.append({
            "export": name,
            "export_rva": f"0x{export_rva:X}",
            "resolver_rva": f"0x{current_resolver_rva:X}",
            "cache_slot_rva": f"0x{slot_rva:X}",
            "bridge_export": bridge_name.rstrip(b"\0").decode("ascii"),
            "relocation_rva": f"0x{relocation_rva:X}",
        })

    strings_offset = pe.get_offset_from_rva(STRINGS_RVA)
    patched[strings_offset:strings_offset + len(names)] = names
    checksum_offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", patched, checksum_offset, 0)
    checksum = pefile.PE(data=bytes(patched), fast_load=True).generate_checksum()
    struct.pack_into("<I", patched, checksum_offset, checksum)

    args.dll.write_bytes(patched)
    result = {
        "path": str(args.dll),
        "candidate": 11,
        "purpose": "replace broken SRW lock and condition variable emulation on Windows 2000",
        "bridge": "w2ksync.dll",
        "w2k_section_characteristics": "0xE0000020",
        "functions": records,
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
