#!/usr/bin/env python3
"""Print selected PE imports and exports for compatibility-DLL analysis."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def decode(value: bytes | None) -> str:
    return value.decode("ascii", errors="replace") if value else ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pe", type=Path)
    parser.add_argument("--dll", help="only show imports from this DLL")
    parser.add_argument("--export", help="only show exports containing this text")
    parser.add_argument("--module-path", type=Path, action="append", default=[])
    args = parser.parse_args()
    for module_path in args.module_path:
        sys.path.insert(0, str(module_path))
    import pefile  # type: ignore

    pe = pefile.PE(str(args.pe), fast_load=True)
    pe.parse_data_directories(
        directories=[
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"],
        ]
    )
    wanted_dll = args.dll.lower() if args.dll else None
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll_name = decode(entry.dll)
            if wanted_dll and dll_name.lower() != wanted_dll:
                continue
            print(f"IMPORT_DLL {dll_name}")
            for symbol in entry.imports:
                name = decode(symbol.name) or f"#{symbol.ordinal}"
                print(f"  IMPORT name={name} iat_rva=0x{symbol.address - pe.OPTIONAL_HEADER.ImageBase:08X}")
    needle = args.export.lower() if args.export else None
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        print(
            f"EXPORT_DLL {decode(pe.DIRECTORY_ENTRY_EXPORT.name)} "
            f"base={pe.DIRECTORY_ENTRY_EXPORT.struct.Base}"
        )
        for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            name = decode(symbol.name) or ""
            if needle and needle not in name.lower():
                continue
            forwarder = decode(symbol.forwarder)
            print(
                f"  EXPORT ordinal={symbol.ordinal} name={name or '<none>'} "
                f"rva=0x{symbol.address:08X} forwarder={forwarder or '-'}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
