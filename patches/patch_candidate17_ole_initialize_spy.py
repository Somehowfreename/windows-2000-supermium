#!/usr/bin/env python3
"""Fix the Windows 2000 OLE initialize-spy fallback ABI.

The proxy's generated unimplemented stubs use a cdecl-style plain RET. Both
CoRegisterInitializeSpy and CoRevokeInitializeSpy are stdcall functions with
eight argument bytes. Returning without popping those bytes moves Chromium's
stack-cookie slot and triggers __report_gsfailure in ComInitBalancer.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


P_OLE_INPUT_SHA256 = "7F9A6FC69D333D69353480649D0DF39F9A4C8C8F1B71732D4F61C3A164B37A14"
CO_REGISTER_INITIALIZE_SPY_RVA = 0xA270
CO_REVOKE_INITIALIZE_SPY_RVA = 0xA330
E_NOTIMPL = 0x80004001


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def update_checksum(data: bytearray) -> int:
    pe = pefile.PE(data=bytes(data), fast_load=True)
    offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", data, offset, 0)
    checksum = pefile.PE(data=bytes(data), fast_load=True).generate_checksum()
    struct.pack_into("<I", data, offset, checksum)
    return checksum


def patch_stub(data: bytearray, pe: pefile.PE, rva: int,
               message_va: int, dbgprint_va: int) -> dict[str, object]:
    offset = pe.get_offset_from_rva(rva)
    call_displacement = dbgprint_va - (pe.OPTIONAL_HEADER.ImageBase + rva + 10)
    expected = (
        b"\x68" + struct.pack("<I", message_va) +
        b"\xE8" + struct.pack("<i", call_displacement) +
        b"\x59\xC3"
    )
    if bytes(data[offset:offset + len(expected)]) != expected:
        raise SystemExit(f"unexpected unimplemented stub at RVA 0x{rva:X}")
    # mov eax,E_NOTIMPL; ret 8; nop*4
    replacement = (
        b"\xB8" + struct.pack("<I", E_NOTIMPL) +
        b"\xC2\x08\x00" + b"\x90" * 4
    )
    data[offset:offset + len(replacement)] = replacement
    return {
        "rva": f"0x{rva:X}",
        "behavior": "return E_NOTIMPL and pop 8 stdcall argument bytes",
        "bytes_before": expected.hex(" ").upper(),
        "bytes_after": replacement.hex(" ").upper(),
    }


def patch_p_ole(path: Path) -> dict[str, object]:
    before = path.read_bytes()
    if digest(before) != P_OLE_INPUT_SHA256:
        raise SystemExit("p_ole.dll is not the validated Candidate 15 input")
    pe = pefile.PE(data=before, fast_load=False)
    patched = bytearray(before)
    stubs = [
        patch_stub(patched, pe, CO_REGISTER_INITIALIZE_SPY_RVA,
                   0x1031E508, 0x103190A0),
        patch_stub(patched, pe, CO_REVOKE_INITIALIZE_SPY_RVA,
                   0x1031E70C, 0x103190A0),
    ]
    checksum = update_checksum(patched)
    path.write_bytes(patched)
    return {
        "sha256_before": digest(before),
        "sha256_after": digest(patched),
        "stubs": stubs,
        "checksum": f"0x{checksum:08X}",
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("supermium", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    result = {
        "candidate": 17,
        "purpose": (
            "fix CoRegisterInitializeSpy and CoRevokeInitializeSpy fallback "
            "stack cleanup on Windows 2000"
        ),
        "root_cause": (
            "cdecl-style unimplemented proxy stubs returned without popping "
            "eight stdcall argument bytes, causing ComInitBalancer's GS cookie "
            "check to fail"
        ),
        "p_ole": patch_p_ole(args.supermium / "p_ole.dll"),
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
