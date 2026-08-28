#!/usr/bin/env python3
"""Run Chromium's Network Service in-process for native Windows 2000."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


INPUT_SHA256 = "0EE4224547FEF1ACEE80DEEEEE6CEC3EB4BB4D44FE870A4948271A78449BB2EA"
IS_IN_PROCESS_NETWORK_SERVICE_RVA = 0x009950B0
EXPECTED_THUNK = bytes.fromhex("55 89 E5 5D E9 07 00 00 00")
PATCH = bytes.fromhex("B0 01 C3 90 90 90 90 90 90")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("chrome", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    before = args.chrome.read_bytes()
    if digest(before) != INPUT_SHA256:
        raise SystemExit("input is not the validated Candidate 13 chrome.dll")

    pe = pefile.PE(data=before, fast_load=True)
    function_offset = pe.get_offset_from_rva(IS_IN_PROCESS_NETWORK_SERVICE_RVA)
    actual = before[function_offset : function_offset + len(EXPECTED_THUNK)]
    if actual != EXPECTED_THUNK:
        raise SystemExit(
            "unexpected IsInProcessNetworkService thunk: " + actual.hex(" ")
        )

    patched = bytearray(before)
    patched[function_offset : function_offset + len(PATCH)] = PATCH
    checksum_offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", patched, checksum_offset, 0)
    checksum = pefile.PE(data=bytes(patched), fast_load=True).generate_checksum()
    struct.pack_into("<I", patched, checksum_offset, checksum)
    args.chrome.write_bytes(patched)

    result = {
        "candidate": 14,
        "purpose": (
            "host Chromium's Network Service inside the browser process on "
            "Windows 2000, bypassing its repeatedly terminating utility-process "
            "launch boundary"
        ),
        "symbol": "content::IsInProcessNetworkService",
        "source": "content/public/browser/network_service_util.cc:16",
        "rva": f"0x{IS_IN_PROCESS_NETWORK_SERVICE_RVA:08X}",
        "replacement": "return true",
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
