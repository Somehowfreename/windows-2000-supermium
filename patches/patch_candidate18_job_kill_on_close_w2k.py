#!/usr/bin/env python3
"""Make Chromium's sandbox job initialization compatible with Windows 2000.

Windows 2000 implements job objects, active-process limits, unhandled-exception
limits, and UI restrictions. It rejects JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
introduced after Windows 2000, with ERROR_INVALID_PARAMETER. Chromium always
adds that flag, causing sandbox::Job::Init to return launch result 47 before a
GPU or renderer child can be created.

Route pwrp_k32!SetInformationJobObject through a narrow shim that clears only
that unsupported bit for JobObjectExtendedLimitInformation, then forwards to
the already-resolved native kernel32 function.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


PWRP_INPUT_SHA256 = "9B7A3DC263D1AA7C6FB857374B5B38020B7ADAB32DC3593D9B89222C0517B860"
SET_INFORMATION_JOB_OBJECT_EXPORT_RVA = 0x1B882
SET_INFORMATION_JOB_OBJECT_SLOT_RVA = 0x6A5C4
HOOK_RVA = 0x73C00
JOB_OBJECT_EXTENDED_LIMIT_INFORMATION = 9
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000
IMAGE_REL_BASED_HIGHLOW = 3


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def relative_branch(source_rva: int, target_rva: int) -> bytes:
    return b"\xE9" + struct.pack("<i", target_rva - (source_rva + 5))


def compatibility_hook() -> bytes:
    code = bytearray()
    code += b"\x53"                           # push ebx
    code += b"\xE8\x00\x00\x00\x00"         # call next instruction
    code += b"\x5B"                           # pop ebx
    code += b"\x81\xEB" + struct.pack("<I", HOOK_RVA + 6)
    # The push above shifts the original stdcall arguments by four bytes.
    code += b"\x83\x7C\x24\x0C" + bytes([JOB_OBJECT_EXTENDED_LIMIT_INFORMATION])
    not_extended = len(code)
    code += b"\x75\x00"                     # jne forward
    code += b"\x8B\x44\x24\x10"           # mov eax, information
    code += b"\x85\xC0"                     # test eax, eax
    null_information = len(code)
    code += b"\x74\x00"                     # je forward
    code += b"\x81\x60\x10" + struct.pack(
        "<I", (~JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) & 0xFFFFFFFF
    )                                         # and [eax+LimitFlags], ~0x2000
    forward = len(code)
    code += b"\x8B\x83" + struct.pack("<I", SET_INFORMATION_JOB_OBJECT_SLOT_RVA)
    code += b"\x5B\xFF\xE0"                 # pop ebx; jmp eax

    for branch in (not_extended, null_information):
        displacement = forward - (branch + 2)
        if not -128 <= displacement <= 127:
            raise ValueError("hook branch is out of range")
        code[branch + 1] = displacement & 0xFF
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
        raise SystemExit("pwrp_k32.dll is not the validated Candidate 15 input")
    pe = pefile.PE(data=before, fast_load=False)
    section = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".w2k")
    if section.Characteristics != 0xE0000020:
        raise SystemExit("Candidate 15 .w2k section is not RWX")

    code = compatibility_hook()
    hook_offset = pe.get_offset_from_rva(HOOK_RVA)
    if before[hook_offset:hook_offset + len(code)] != b"\0" * len(code):
        raise SystemExit("Candidate 18 compatibility-hook code cave is not empty")

    export_offset = pe.get_offset_from_rva(SET_INFORMATION_JOB_OBJECT_EXPORT_RVA)
    expected_thunk = b"\xFF\x25" + struct.pack(
        "<I", pe.OPTIONAL_HEADER.ImageBase + SET_INFORMATION_JOB_OBJECT_SLOT_RVA
    )
    if before[export_offset:export_offset + 6] != expected_thunk:
        raise SystemExit("unexpected SetInformationJobObject export trampoline")

    patched = bytearray(before)
    patched[hook_offset:hook_offset + len(code)] = code
    patched[export_offset:export_offset + 6] = (
        relative_branch(SET_INFORMATION_JOB_OBJECT_EXPORT_RVA, HOOK_RVA) + b"\x90"
    )

    relocation = next(
        entry
        for block in pe.DIRECTORY_ENTRY_BASERELOC
        for entry in block.entries
        if entry.rva == SET_INFORMATION_JOB_OBJECT_EXPORT_RVA + 2
    )
    if relocation.type != IMAGE_REL_BASED_HIGHLOW:
        raise SystemExit("unexpected SetInformationJobObject relocation type")
    relocation_offset = relocation.struct.get_file_offset()
    word = struct.unpack_from("<H", before, relocation_offset)[0]
    struct.pack_into("<H", patched, relocation_offset, word & 0x0FFF)

    checksum = update_checksum(patched)
    path.write_bytes(patched)
    return {
        "sha256_before": digest(before),
        "sha256_after": digest(patched),
        "export_rva": f"0x{SET_INFORMATION_JOB_OBJECT_EXPORT_RVA:X}",
        "original_slot_rva": f"0x{SET_INFORMATION_JOB_OBJECT_SLOT_RVA:X}",
        "hook_rva": f"0x{HOOK_RVA:X}",
        "hook_size": len(code),
        "cleared_flag": f"0x{JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE:08X}",
        "information_class": JOB_OBJECT_EXTENDED_LIMIT_INFORMATION,
        "checksum": f"0x{checksum:08X}",
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    result = {
        "candidate": 18,
        "purpose": (
            "retain Chromium's Windows sandbox on Windows 2000 by removing "
            "only the unsupported JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE bit"
        ),
        "native_probe": {
            "extended_without_flag": "success",
            "extended_with_flag": "ERROR_INVALID_PARAMETER (87)",
            "active_process_limit": "success",
            "unhandled_exception_limit": "success in extended information",
            "ui_restrictions": "success",
        },
        "pwrp": patch_pwrp(args.dll),
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
