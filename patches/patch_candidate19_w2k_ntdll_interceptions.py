#!/usr/bin/env python3
"""Skip sandbox interceptions for ntdll exports absent on Windows 2000.

Windows 2000 exports NtOpenProcessToken and NtOpenThreadToken, but not their
later NtOpenProcessTokenEx and NtOpenThreadTokenEx variants. Chromium registers
all four interceptions unconditionally. Its service resolver deliberately
traps when an expected export is absent, before the sandboxed child is resumed.

This W2K-only patch jumps over registration of the two nonexistent APIs. Every
interception for an API actually present on Windows 2000 remains enabled.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


CHROME_EXE_INPUT_SHA256 = "10A50B3EBABDACDDC9063C7B4429E80E85D8600DF89990E7A321C54CFEA16209"
SKIP_FROM_RVA = 0x0003B70D
SKIP_TO_RVA = 0x0003B749
EXPECTED = bytes.fromhex("89 F1 6A 08 68")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def relative_branch(source_rva: int, target_rva: int) -> bytes:
    return b"\xE9" + struct.pack("<i", target_rva - (source_rva + 5))


def update_checksum(data: bytearray) -> int:
    pe = pefile.PE(data=bytes(data), fast_load=True)
    offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", data, offset, 0)
    checksum = pefile.PE(data=bytes(data), fast_load=True).generate_checksum()
    struct.pack_into("<I", data, offset, checksum)
    return checksum


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("chrome_exe", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.chrome_exe.read_bytes()
    if digest(before) != CHROME_EXE_INPUT_SHA256:
        raise SystemExit("chrome.exe is not the validated Supermium 144 R5 input")
    pe = pefile.PE(data=before, fast_load=True)
    offset = pe.get_offset_from_rva(SKIP_FROM_RVA)
    if before[offset:offset + len(EXPECTED)] != EXPECTED:
        raise SystemExit("unexpected SetupBasicInterceptions instruction bytes")

    patched = bytearray(before)
    patch = relative_branch(SKIP_FROM_RVA, SKIP_TO_RVA)
    patched[offset:offset + len(patch)] = patch
    checksum = update_checksum(patched)
    args.chrome_exe.write_bytes(patched)

    result = {
        "candidate": 19,
        "purpose": (
            "retain the native Chromium sandbox on Windows 2000 while "
            "skipping only two interception targets not exported by its ntdll"
        ),
        "native_probe": {
            "NtOpenProcessTokenEx": "absent, GetProcAddress error 127",
            "NtOpenThreadTokenEx": "absent, GetProcAddress error 127",
            "NtOpenProcessToken": "present",
            "NtOpenThreadToken": "present",
            "other_required_Nt_interception_targets": "present",
        },
        "chrome_exe": {
            "sha256_before": digest(before),
            "sha256_after": digest(patched),
            "skip_from_rva": f"0x{SKIP_FROM_RVA:08X}",
            "skip_to_rva": f"0x{SKIP_TO_RVA:08X}",
            "replacement": patch.hex(" ").upper(),
            "checksum": f"0x{checksum:08X}",
        },
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
