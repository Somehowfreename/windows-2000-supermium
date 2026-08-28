#!/usr/bin/env python3
"""Append a PE export section while preserving ordinals from a reference DLL."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

LOCAL_PACKAGES = Path(__file__).resolve().parents[1] / "python-packages"
if LOCAL_PACKAGES.is_dir():
    sys.path.insert(0, str(LOCAL_PACKAGES))

import pefile


STDCALL = re.compile(rb"^_(.+)@[0-9]+$")


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def canonical_export_name(name: bytes) -> bytes:
    match = STDCALL.match(name)
    return match.group(1) if match else name


def load_exports(path: Path) -> tuple[pefile.PE, dict[bytes, tuple[int, int]]]:
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(
        [pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]]
    )
    exports: dict[bytes, tuple[int, int]] = {}
    for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if not symbol.name or symbol.forwarder:
            continue
        name = canonical_export_name(symbol.name)
        if name in exports:
            raise SystemExit(f"duplicate canonical export {name!r} in {path}")
        exports[name] = (symbol.ordinal, symbol.address)
    return pe, exports


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--dll-name", default="p_s232.dll")
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    input_pe, input_exports = load_exports(args.input)
    _, reference_exports = load_exports(args.reference)
    missing = sorted(name for name in input_exports if name not in reference_exports)
    if missing:
        raise SystemExit(
            "replacement exports absent from reference: "
            + ", ".join(name.decode("ascii", "replace") for name in missing)
        )

    assignments: list[tuple[bytes, int, int]] = []
    used_ordinals: dict[int, bytes] = {}
    for name, (_, rva) in input_exports.items():
        ordinal = reference_exports[name][0]
        if ordinal in used_ordinals:
            raise SystemExit(
                f"ordinal collision {ordinal}: {used_ordinals[ordinal]!r} and {name!r}"
            )
        used_ordinals[ordinal] = name
        assignments.append((name, ordinal, rva))
    assignments.sort(key=lambda item: item[0])
    ordinal_base = min(ordinal for _, ordinal, _ in assignments)
    if ordinal_base != 1:
        raise SystemExit(f"expected ordinal base 1, got {ordinal_base}")
    function_count = max(ordinal for _, ordinal, _ in assignments)

    data = bytearray(args.input.read_bytes())
    file_alignment = input_pe.OPTIONAL_HEADER.FileAlignment
    section_alignment = input_pe.OPTIONAL_HEADER.SectionAlignment
    section_count = input_pe.FILE_HEADER.NumberOfSections
    section_table = (
        input_pe.DOS_HEADER.e_lfanew
        + 4
        + input_pe.FILE_HEADER.sizeof()
        + input_pe.FILE_HEADER.SizeOfOptionalHeader
    )
    new_section_header = section_table + section_count * 40
    add_section = new_section_header + 40 <= input_pe.OPTIONAL_HEADER.SizeOfHeaders
    extended_section = None
    if add_section:
        virtual_end = max(
            section.VirtualAddress
            + max(section.Misc_VirtualSize, section.SizeOfRawData)
            for section in input_pe.sections
        )
        new_virtual_address = align(virtual_end, section_alignment)
        new_raw_offset = align(len(data), file_alignment)
    else:
        extended_section = max(
            input_pe.sections,
            key=lambda section: section.PointerToRawData + section.SizeOfRawData,
        )
        raw_end = extended_section.PointerToRawData + extended_section.SizeOfRawData
        if raw_end != len(data):
            raise SystemExit(
                "no section-header room and last raw section does not end at EOF"
            )
        new_raw_offset = raw_end
        new_virtual_address = (
            extended_section.VirtualAddress + extended_section.SizeOfRawData
        )

    export_directory_size = 40
    eat_offset = align(export_directory_size, 4)
    names_offset = eat_offset + function_count * 4
    ordinals_offset = names_offset + len(assignments) * 4
    strings_offset = align(ordinals_offset + len(assignments) * 2, 4)
    blob = bytearray(strings_offset)

    dll_name = args.dll_name.encode("ascii") + b"\0"
    dll_name_offset = len(blob)
    blob.extend(dll_name)
    name_offsets: dict[bytes, int] = {}
    for name, _, _ in assignments:
        name_offsets[name] = len(blob)
        blob.extend(name + b"\0")

    for name, ordinal, function_rva in assignments:
        struct.pack_into("<I", blob, eat_offset + (ordinal - 1) * 4, function_rva)
    for index, (name, ordinal, _) in enumerate(assignments):
        struct.pack_into(
            "<I", blob, names_offset + index * 4,
            new_virtual_address + name_offsets[name]
        )
        struct.pack_into("<H", blob, ordinals_offset + index * 2, ordinal - 1)

    struct.pack_into(
        "<IIHHIIIIIII",
        blob,
        0,
        0,
        input_pe.FILE_HEADER.TimeDateStamp,
        0,
        0,
        new_virtual_address + dll_name_offset,
        ordinal_base,
        function_count,
        len(assignments),
        new_virtual_address + eat_offset,
        new_virtual_address + names_offset,
        new_virtual_address + ordinals_offset,
    )
    raw_size = align(len(blob), file_alignment)
    blob.extend(b"\0" * (raw_size - len(blob)))

    if len(data) < new_raw_offset:
        data.extend(b"\0" * (new_raw_offset - len(data)))
    data.extend(blob)
    if add_section:
        section_header = struct.pack(
            "<8sIIIIIIHHI",
            b".w2kexp\0",
            len(blob),
            new_virtual_address,
            raw_size,
            new_raw_offset,
            0,
            0,
            0,
            0,
            0x40000040,
        )
        data[new_section_header : new_section_header + 40] = section_header
        struct.pack_into(
            "<H", data,
            input_pe.FILE_HEADER.get_field_absolute_offset("NumberOfSections"),
            section_count + 1,
        )
    else:
        assert extended_section is not None
        relative_end = (
            new_raw_offset - extended_section.PointerToRawData + raw_size
        )
        section_header_offset = extended_section.get_file_offset()
        struct.pack_into("<I", data, section_header_offset + 8, relative_end)
        struct.pack_into("<I", data, section_header_offset + 16, relative_end)
        characteristics = (
            extended_section.Characteristics & ~0x02000000
        ) | 0x40000040
        struct.pack_into("<I", data, section_header_offset + 36, characteristics)
    struct.pack_into(
        "<I", data,
        input_pe.OPTIONAL_HEADER.get_field_absolute_offset("SizeOfImage"),
        align(new_virtual_address + raw_size, section_alignment),
    )
    initialized_offset = input_pe.OPTIONAL_HEADER.get_field_absolute_offset(
        "SizeOfInitializedData"
    )
    struct.pack_into(
        "<I", data, initialized_offset,
        input_pe.OPTIONAL_HEADER.SizeOfInitializedData + raw_size,
    )
    export_entry = input_pe.OPTIONAL_HEADER.DATA_DIRECTORY[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]
    ]
    struct.pack_into(
        "<II",
        data,
        export_entry.get_field_absolute_offset("VirtualAddress"),
        new_virtual_address,
        len(blob),
    )

    checksum_offset = input_pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", data, checksum_offset, 0)
    checksum = pefile.PE(data=bytes(data), fast_load=True).generate_checksum()
    struct.pack_into("<I", data, checksum_offset, checksum)
    args.output.write_bytes(data)

    validation_pe, validation_exports = load_exports(args.output)
    validation = {
        name.decode("ascii"): {
            "ordinal": validation_exports[name][0],
            "rva": f"0x{validation_exports[name][1]:08X}",
        }
        for name, _, _ in assignments
    }
    manifest = {
        "input": str(args.input),
        "reference": str(args.reference),
        "output": str(args.output),
        "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest().upper(),
        "checksum": f"0x{validation_pe.OPTIONAL_HEADER.CheckSum:08X}",
        "section_rva": f"0x{new_virtual_address:08X}",
        "section_raw_offset": f"0x{new_raw_offset:08X}",
        "section_strategy": "new-section" if add_section else "extend-last-section",
        "exports": validation,
    }
    rendered = json.dumps(manifest, indent=2) + "\n"
    if args.manifest:
        args.manifest.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
