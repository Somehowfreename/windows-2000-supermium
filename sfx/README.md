# Windows 2000 self-extractor

`w2k_sfx.c` is a small native GUI stub intended to run on untouched Windows 2000. `build_w2k_sfx.mjs` creates an LZX CAB from a source tree, appends the CAB to the stub, and writes a fixed metadata footer.

The stub:

- clearly states that it only extracts files;
- lets the user choose a parent destination;
- validates the embedded CAB with CRC-32 before extraction;
- rejects absolute paths, drive-qualified paths, and `..` traversal components;
- extracts with the Windows 2000 `SetupIterateCabinetA` API;
- reports the exact extracted-file count and destination;
- deletes its temporary CAB.

Both final EXEs passed independent host CAB extraction/byte comparison and live extraction on unupdated Windows 2000 SP4. See `docs/BUILDING.md`.
