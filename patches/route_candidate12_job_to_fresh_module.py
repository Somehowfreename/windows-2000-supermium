#!/usr/bin/env python3
"""Route Candidate 12's job bridge to a fresh W2K module filename."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


INPUT_SHA256 = "69B69CE206D907819244DB3C11A67042F4C57B1920D9D60E183B7017D4EDAD42"
RESOLVER_DLL_RVA_FIELD = 0x737D9
OLD_DLL_NAME_RVA = 0x73A00
NEW_DLL_NAME_RVA = 0x73B8A
NEW_DLL_NAME = b"w2kjob2.dll\0"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.dll.read_bytes()
    if digest(before) != INPUT_SHA256:
        raise SystemExit("input is not the validated initial Candidate 12 pwrp")
    pe = pefile.PE(data=before, fast_load=True)
    field_offset = pe.get_offset_from_rva(RESOLVER_DLL_RVA_FIELD)
    if struct.unpack_from("<I", before, field_offset)[0] != OLD_DLL_NAME_RVA:
        raise SystemExit("unexpected job resolver DLL-name RVA")
    name_offset = pe.get_offset_from_rva(NEW_DLL_NAME_RVA)
    if before[name_offset:name_offset + len(NEW_DLL_NAME)] != b"\0" * len(NEW_DLL_NAME):
        raise SystemExit("fresh job-module name area is not empty")

    patched = bytearray(before)
    struct.pack_into("<I", patched, field_offset, NEW_DLL_NAME_RVA)
    patched[name_offset:name_offset + len(NEW_DLL_NAME)] = NEW_DLL_NAME
    checksum_offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", patched, checksum_offset, 0)
    checksum = pefile.PE(data=bytes(patched), fast_load=True).generate_checksum()
    struct.pack_into("<I", patched, checksum_offset, checksum)
    args.dll.write_bytes(patched)

    result = {
        "candidate": 12,
        "purpose": "avoid Windows 2000's cached w2ksync image for the new job export",
        "module": NEW_DLL_NAME.rstrip(b"\0").decode("ascii"),
        "module_name_rva": f"0x{NEW_DLL_NAME_RVA:X}",
        "resolver_field_rva": f"0x{RESOLVER_DLL_RVA_FIELD:X}",
        "checksum": f"0x{checksum:08X}",
        "sha256_before": digest(before),
        "sha256_after": digest(patched),
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
