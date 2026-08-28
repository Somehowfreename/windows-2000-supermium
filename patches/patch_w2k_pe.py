#!/usr/bin/env python3
"""Apply loader-level Windows 2000 compatibility fixes to a Supermium tree."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

import pefile


TARGET_VERSION = (5, 0)
NTDLL_NAME = b"ntdll.dll\x00"
NTDLL_WRAPPER_NAME = b"p_ntd.dll\x00"


def patch(path: Path) -> dict[str, object] | None:
    try:
        pe = pefile.PE(str(path), fast_load=False)
    except pefile.PEFormatError:
        return None

    before_os = (
        pe.OPTIONAL_HEADER.MajorOperatingSystemVersion,
        pe.OPTIONAL_HEADER.MinorOperatingSystemVersion,
    )
    before_subsystem = (
        pe.OPTIONAL_HEADER.MajorSubsystemVersion,
        pe.OPTIONAL_HEADER.MinorSubsystemVersion,
    )
    redirects: list[str] = []
    import_offsets: list[int] = []

    for desc in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        names = {entry.name for entry in desc.imports if entry.name}
        if desc.dll.lower() == b"ntdll.dll" and b"RtlCaptureContext" in names:
            offset = pe.get_offset_from_rva(desc.struct.Name)
            import_offsets.append(offset)
            redirects.append("ntdll.dll->p_ntd.dll (RtlCaptureContext)")

    optional_offset = pe.DOS_HEADER.e_lfanew + 24
    changed = before_os != TARGET_VERSION or before_subsystem != TARGET_VERSION or bool(import_offsets)
    pe.close()
    if not changed:
        return None

    with path.open("r+b") as stream:
        if before_os != TARGET_VERSION:
            stream.seek(optional_offset + 40)
            stream.write(struct.pack("<HH", *TARGET_VERSION))
        if before_subsystem != TARGET_VERSION:
            stream.seek(optional_offset + 48)
            stream.write(struct.pack("<HH", *TARGET_VERSION))
        for offset in import_offsets:
            stream.seek(offset)
            current = stream.read(len(NTDLL_NAME))
            if current.lower() != NTDLL_NAME:
                raise RuntimeError(f"unexpected import string at {path}:{offset}: {current!r}")
            stream.seek(offset)
            stream.write(NTDLL_WRAPPER_NAME)

    return {
        "path": str(path),
        "os_version": {"before": before_os, "after": TARGET_VERSION},
        "subsystem_version": {"before": before_subsystem, "after": TARGET_VERSION},
        "import_redirects": redirects,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    changes = []
    for path in sorted(args.root.rglob("*")):
        if path.is_file() and path.suffix.casefold() in {".dll", ".exe"}:
            record = patch(path)
            if record:
                changes.append(record)

    output = json.dumps(changes, indent=2)
    if args.manifest:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(output + "\n", encoding="utf-8")
    else:
        print(output)


if __name__ == "__main__":
    main()
