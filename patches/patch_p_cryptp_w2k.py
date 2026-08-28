#!/usr/bin/env python3
"""Make Supermium's ProcessPrng CryptoAPI fallback valid on Windows 2000."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import pefile


PATCH_RVA = 0x12A8
BEFORE = bytes.fromhex("6A 18")  # push PROV_RSA_AES (24)
AFTER = bytes.fromhex("6A 01")   # push PROV_RSA_FULL (1)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    original = args.dll.read_bytes()
    pe = pefile.PE(data=original, fast_load=False)
    offset = pe.get_offset_from_rva(PATCH_RVA)
    pe.close()
    found = original[offset : offset + len(BEFORE)]
    if found == AFTER:
        changed = False
        patched = original
    elif found == BEFORE:
        changed = True
        patched = original[:offset] + AFTER + original[offset + len(AFTER) :]
        args.dll.write_bytes(patched)
    else:
        raise SystemExit(
            f"unexpected bytes at RVA 0x{PATCH_RVA:X} / file offset 0x{offset:X}: "
            f"{found.hex(' ').upper()}"
        )

    result = {
        "path": str(args.dll),
        "reason": (
            "Windows 2000 does not define PROV_RSA_AES (24); use "
            "PROV_RSA_FULL (1) for the CRYPT_VERIFYCONTEXT random provider"
        ),
        "rva": f"0x{PATCH_RVA:X}",
        "file_offset": f"0x{offset:X}",
        "before": BEFORE.hex(" ").upper(),
        "after": AFTER.hex(" ").upper(),
        "changed": changed,
        "sha256_before": digest(original),
        "sha256_after": digest(patched),
    }
    output = json.dumps(result, indent=2)
    if args.manifest:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(output + "\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
