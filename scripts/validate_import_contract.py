#!/usr/bin/env python3
"""Verify one PE's imports from a DLL are satisfied by a replacement DLL."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

LOCAL_PACKAGES = Path(__file__).resolve().parents[1] / "python-packages"
if LOCAL_PACKAGES.is_dir():
    sys.path.insert(0, str(LOCAL_PACKAGES))

import pefile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("consumer", type=Path)
    parser.add_argument("dll_name")
    parser.add_argument("replacement", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    consumer = pefile.PE(str(args.consumer), fast_load=True)
    consumer.parse_data_directories(
        [pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]]
    )
    replacement = pefile.PE(str(args.replacement), fast_load=True)
    replacement.parse_data_directories(
        [pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]]
    )
    exports_by_name = {
        symbol.name.decode("ascii"): symbol.ordinal
        for symbol in replacement.DIRECTORY_ENTRY_EXPORT.symbols
        if symbol.name
    }
    exports_by_ordinal = {
        symbol.ordinal: (
            symbol.name.decode("ascii") if symbol.name else None
        )
        for symbol in replacement.DIRECTORY_ENTRY_EXPORT.symbols
        if symbol.address
    }
    imports: list[dict[str, object]] = []
    missing: list[str] = []
    matched_dll = False
    for entry in consumer.DIRECTORY_ENTRY_IMPORT:
        dll = entry.dll.decode("ascii")
        if dll.lower() != args.dll_name.lower():
            continue
        matched_dll = True
        for symbol in entry.imports:
            if symbol.name:
                name = symbol.name.decode("ascii")
                satisfied = name in exports_by_name
                rendered = name
                resolved = exports_by_name.get(name)
            else:
                ordinal = symbol.ordinal
                satisfied = ordinal in exports_by_ordinal
                rendered = f"#{ordinal}"
                resolved = exports_by_ordinal.get(ordinal)
            imports.append(
                {
                    "import": rendered,
                    "satisfied": satisfied,
                    "replacement": resolved,
                }
            )
            if not satisfied:
                missing.append(rendered)
    if not matched_dll:
        raise SystemExit(f"consumer does not import {args.dll_name}")
    result = {
        "consumer": str(args.consumer),
        "dll_name": args.dll_name,
        "replacement": str(args.replacement),
        "import_count": len(imports),
        "missing": missing,
        "imports": imports,
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
