Release date: 2026-08-25

## Scope

This is Release Candidate 1 of the native Windows 2000 x86 compatibility build for Supermium `144.0.7559.256 R5`. It is version-locked to upstream tag `v144-r5`, commit `0cdb6d9aa53c96875f637f7303115fb8fdd9d502`.

The release preserves the working legacy Chromium renderer sandbox and does not use One-Core-API, an extended kernel, replacement system DLLs, or `--no-sandbox`.

## Installation choices

- Browser ZIP for users who already have an archive tool.
- Native browser self-extracting EXE tested on unupdated Windows 2000 SP4.
- Certificate ZIP for users who already have an archive tool.
- Native certificate self-extracting EXE tested on unupdated Windows 2000 SP4.

The EXEs only extract files. Browser execution requires SP4, all applicable Legacy Update updates, current root certificates, and suitable hardware drivers.

## Test highlights

- Official Microsoft Windows 2000 Professional SP4 installation, fully updated through Legacy Update.
- Exact final ZIP: 315-file internal SHA-256 manifest passed, clean extraction passed, native launch passed.
- Browser version `144.0.7559.256`; sandbox enabled; no `--no-sandbox`.
- TLS 1.3 and modern-site matrix passed for GitHub, Reddit, YouTube, and issuer-diverse HTTPS endpoints.
- Default YouTube AV1/Opus: 57.504911 seconds progressed, 1,529 decoded frames, 9 dropped (0.58862%), decoded audio bytes, 24 media responses, no media error.
- h264ify-compatible H.264/AAC: 57.958764 seconds progressed, 1,738 decoded frames, 7 dropped (0.40276%), decoded audio bytes, 29 media responses, no media error.
- Pause, resume, seeking, volume-state changes, and tab closure passed.
- uBlock Origin `1.73.0`, AdBlock `6.44.0`, and h264ify `2.0.1` install/permission/lifecycle/persistence checks passed without visiting advertising sites.
- Explicit diagnostic session: YouTube monitor available, 66 valid media records, no disallowed personal fields, browser exit code 0, and temporary private profile cleanup complete.
- Certificate installer: 121 roots added and verified; test store count changed from 191 to 312 and persisted after reboot; controlled local-root trust behavior and public TLS 1.3 tests passed.
- Bare unupdated SP4 self-extraction: 316 browser files and 133 certificate-package files completed successfully.

## Release Candidate 1 qualification

Browser-level video and audio decoding works. Audible output could not be judged reliably because the VirtualBox Windows 2000 guest repeated the same short buffer while playing ordinary native Windows sounds, independently of Supermium. Bare-metal reports are requested, especially for YouTube audio, graphics, timing, and sound-card drivers.

## Known limits

- Bare-metal audible YouTube confirmation remains outstanding.
- WebGPU is not claimed.
- Widevine/Netflix is not claimed.
- Modern web services may change after this release.
- Windows 2000 remains an unsupported operating system with unresolved platform security risks.
