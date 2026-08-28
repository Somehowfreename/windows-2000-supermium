#!/usr/bin/env python3
"""Patch Candidate 11 with a W2K IsProcessInJob bridge and PDH fallback."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


PWRP_INPUT_SHA256 = "0A66CF8A1E7D038159836E848A9366D2085F74AC086D0A2E15CD82215C9908D6"
W2KSYNC_SHA256 = "F1EDAC1B7975E8C44067B33C91112FA4999340424078DDBE4E163E25677116AC"
IS_PROCESS_IN_JOB_EXPORT_RVA = 0x1AE20
RESOLVER_RVA = 0x737C0
SLOT_RVA = 0x73930
STRINGS_RVA = 0x73A00
LOAD_LIBRARY_A_IAT_RVA = 0x2D10C
GET_PROC_ADDRESS_IAT_RVA = 0x2D0B8
DLL_NAME = b"w2ksync.dll\0"
BRIDGE_NAME = b"_W2KIsProcessInJob@12\0"
IMAGE_REL_BASED_HIGHLOW = 3

EXISTING_BRIDGE_NAMES = [
    b"_W2KAcquireSRWLockExclusive@4\0",
    b"_W2KAcquireSRWLockShared@4\0",
    b"_W2KInitializeConditionVariable@4\0",
    b"_W2KInitializeSRWLock@4\0",
    b"_W2KReleaseSRWLockExclusive@4\0",
    b"_W2KReleaseSRWLockShared@4\0",
    b"_W2KSleepConditionVariableCS@12\0",
    b"_W2KSleepConditionVariableSRW@16\0",
    b"_W2KTryAcquireSRWLockExclusive@4\0",
    b"_W2KTryAcquireSRWLockShared@4\0",
    b"_W2KWakeAllConditionVariable@4\0",
    b"_W2KWakeConditionVariable@4\0",
]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def relative_branch(source_rva: int, target_rva: int) -> bytes:
    return b"\xE9" + struct.pack("<i", target_rva - (source_rva + 5))


def resolver(function_name_rva: int) -> bytes:
    code = bytearray()
    code += b"\x53"                           # push ebx
    code += b"\xE8\x00\x00\x00\x00"         # call next instruction
    code += b"\x5B"                           # pop ebx
    code += b"\x81\xEB" + struct.pack("<I", RESOLVER_RVA + 6)
    code += b"\x8B\x83" + struct.pack("<I", SLOT_RVA)
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
    code += b"\x89\x83" + struct.pack("<I", SLOT_RVA)
    success = len(code)
    code += b"\x5B\xFF\xE0"                 # pop ebx; jmp eax
    failure = len(code)
    code += b"\x5B\x31\xC0\xC2\x0C\x00"   # FALSE; ret 12
    for jump, target in ((cached_jump, success),
                         (load_failure, failure),
                         (proc_failure, failure)):
        displacement = target - (jump + 2)
        if not -128 <= displacement <= 127:
            raise ValueError("resolver branch is out of range")
        code[jump + 1] = displacement & 0xFF
    return bytes(code)


def update_checksum(data: bytearray) -> int:
    pe = pefile.PE(data=bytes(data), fast_load=True)
    offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", data, offset, 0)
    checksum = pefile.PE(data=bytes(data), fast_load=True).generate_checksum()
    struct.pack_into("<I", data, offset, checksum)
    return checksum


def patch_pwrp(path: Path) -> dict[str, object]:
    before = path.read_bytes()
    if digest(before) != PWRP_INPUT_SHA256:
        raise SystemExit("pwrp_k32.dll is not the validated Candidate 11 input")
    pe = pefile.PE(data=before, fast_load=False)
    section = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".w2k")
    if section.Characteristics != 0xE0000020:
        raise SystemExit("Candidate 11 .w2k section is not RWX")
    patched = bytearray(before)
    function_name_rva = (
        STRINGS_RVA + len(DLL_NAME) + sum(map(len, EXISTING_BRIDGE_NAMES))
    )
    code = resolver(function_name_rva)
    for rva, payload, label in (
        (RESOLVER_RVA, code, "resolver"),
        (SLOT_RVA, b"\0" * 4, "slot"),
        (function_name_rva, BRIDGE_NAME, "bridge name"),
    ):
        offset = pe.get_offset_from_rva(rva)
        if before[offset:offset + len(payload)] != b"\0" * len(payload):
            raise SystemExit(f"Candidate 12 {label} area is not empty")
        patched[offset:offset + len(payload)] = payload

    export_offset = pe.get_offset_from_rva(IS_PROCESS_IN_JOB_EXPORT_RVA)
    if before[export_offset:export_offset + 2] != b"\xFF\x25":
        raise SystemExit("unexpected IsProcessInJob export trampoline")
    patched[export_offset:export_offset + 6] = (
        relative_branch(IS_PROCESS_IN_JOB_EXPORT_RVA, RESOLVER_RVA) + b"\x90"
    )
    relocation = next(
        entry
        for block in pe.DIRECTORY_ENTRY_BASERELOC
        for entry in block.entries
        if entry.rva == IS_PROCESS_IN_JOB_EXPORT_RVA + 2
    )
    if relocation.type != IMAGE_REL_BASED_HIGHLOW:
        raise SystemExit("unexpected IsProcessInJob relocation type")
    relocation_offset = relocation.struct.get_file_offset()
    word = struct.unpack_from("<H", before, relocation_offset)[0]
    struct.pack_into("<H", patched, relocation_offset, word & 0x0FFF)
    checksum = update_checksum(patched)
    path.write_bytes(patched)
    return {
        "sha256_before": digest(before),
        "sha256_after": digest(patched),
        "export_rva": f"0x{IS_PROCESS_IN_JOB_EXPORT_RVA:X}",
        "resolver_rva": f"0x{RESOLVER_RVA:X}",
        "slot_rva": f"0x{SLOT_RVA:X}",
        "bridge_name_rva": f"0x{function_name_rva:X}",
        "checksum": f"0x{checksum:08X}",
    }


def patch_chrome(path: Path) -> dict[str, object]:
    before = path.read_bytes()
    old = (
        r"\Hyper-V Hypervisor Logical Processor(_Total)\% Total Run Time"
    ).encode("utf-16le")
    new = r"\Processor(_Total)\% Processor Time".encode("utf-16le")
    if before.count(old) != 1:
        raise SystemExit("expected one Hyper-V PDH counter string")
    offset = before.index(old)
    replacement = new + b"\0" * (len(old) - len(new))
    patched = bytearray(before)
    patched[offset:offset + len(old)] = replacement
    checksum = update_checksum(patched)
    path.write_bytes(patched)
    return {
        "sha256_before": digest(before),
        "sha256_after": digest(patched),
        "file_offset": f"0x{offset:X}",
        "old_counter": old.decode("utf-16le"),
        "new_counter": new.decode("utf-16le"),
        "checksum": f"0x{checksum:08X}",
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("supermium", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    pwrp = args.supermium / "pwrp_k32.dll"
    chrome = args.supermium / "144.0.7559.256" / "chrome.dll"
    sync = args.supermium / "w2ksync.dll"
    if digest(sync.read_bytes()) != W2KSYNC_SHA256:
        raise SystemExit("Candidate 12 does not contain the validated bridge DLL")
    result = {
        "candidate": 12,
        "purpose": [
            "implement IsProcessInJob for the Windows 2000 sandbox launcher",
            "use the Windows 2000 processor PDH counter instead of Hyper-V",
        ],
        "w2ksync_sha256": W2KSYNC_SHA256,
        "pwrp": patch_pwrp(pwrp),
        "chrome": patch_chrome(chrome),
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
