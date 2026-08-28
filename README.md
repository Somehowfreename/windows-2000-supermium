# Supermium 144 R5 for Windows 2000 — Release Candidate 1

This repository contains the Windows 2000 compatibility work, reproducible binary-patch chain, native bridge sources, opt-in diagnostics, certificate tools, tests, and packaging source for an unofficial 32-bit build of [Supermium](https://github.com/win32ss/supermium). It is based on Supermium `v144-r5`, Chromium `144.0.7559.256`, upstream commit `0cdb6d9aa53c96875f637f7303115fb8fdd9d502`.

The browser runs natively on Windows 2000. It does **not** require One-Core-API, an extended kernel, replacement Windows system DLLs, or `--no-sandbox`. The Windows 2000 compatibility launcher enables the working legacy Chromium sandbox.

This is Release Candidate 1 because audible YouTube output still needs confirmation on bare-metal Windows 2000. Browser-level audio decoding and sustained video playback pass in virtual machines, but the available VirtualBox audio device repeated ordinary Windows sounds before Supermium was started, so that VM cannot provide trustworthy audible-output evidence.

## Release downloads

The release offers equivalent ZIP and native self-extracting EXE choices. Windows 2000 has no built-in ZIP extractor, so the EXE is usually easier on a clean machine. Both EXEs only extract files and let you choose the destination; neither silently installs or changes Windows.

| Asset | Size | SHA-256 |
|---|---:|---|
| `Supermium-144.0.7559.256-R5-Windows-2000-RC1-x86.zip` | 147,476,235 bytes | `8A2DD0CB44711210A458E2F1D4B04038F98091845D8E491001FBC70BC7B42CDA` |
| `Supermium-144.0.7559.256-R5-Windows-2000-RC1-x86.exe` | 119,068,331 bytes | `672E114BBDABB0A148C3B4966B4E902BF742A517BF7FF992803A2F37C28B029A` |
| `Certificates-Installer-Windows-2000.zip` | 315,210 bytes | `BA6C68E4FF3915F722B32474D1B5F028E89DBD3B928CC1E62EEB6AAA132BCCAB` |
| `Certificates-Installer-Windows-2000.exe` | 254,779 bytes | `90144F113B67187C144562CB784B1E6285B930E1CC5521C7D69ACB45221C1DC8` |

## Requirements

- 32-bit Windows 2000 Professional or Server with Service Pack 4.
- All applicable post-SP4 operating-system updates. The validated system was updated through [Legacy Update](https://legacyupdate.net/), which officially supports Windows 2000.
- The modern root certificate package from this release.
- An SSE2-capable x86 processor.
- 2 GB RAM and two CPU cores are recommended for dependable startup and demanding modern sites.
- Current graphics, network, and sound drivers for the installed hardware.

The self-extractors were tested successfully on an untouched Windows 2000 Professional SP4 installation with no post-SP4 updates. That means they can unpack the files on a bare installation; it does **not** mean the browser itself can run before Windows is updated.

## Install

1. Install Windows 2000 SP4 and the correct hardware drivers.
2. Download and install the stable Legacy Update client from [legacyupdate.net](https://legacyupdate.net/). Reboot when requested, return to Legacy Update, and repeat until no applicable operating-system updates remain. A driver or repeatedly offered inapplicable optional component may remain in the list.
3. Download either certificate asset. Extract the ZIP, or run the certificate self-extracting EXE and choose a destination.
4. Keep `INSTALL.cmd`, `W2KROOTS.EXE`, and the `certs` directory together. Sign in as an Administrator and run `INSTALL.cmd`. The expected final result is `files=121, present=121, failed=0`. `VERIFY.cmd` can check the store again later.
5. Download either Supermium asset. Extract the complete ZIP, or run the Supermium self-extracting EXE and choose a destination.
6. Run `Supermium W2K RC1.exe` or one of the supplied launchers. Do not add `--no-sandbox`.

The certificate package includes readable documentation, the original downloaded `cacert.pem`, every `.cer` file, SHA-256 checksums, source code, official source URLs, and manual MMC import instructions. See [docs/CERTIFICATES.md](docs/CERTIFICATES.md).

## What is validated

- Native startup on a clean, fully updated Windows 2000 SP4 installation made from an original Microsoft ISO.
- Chrome `144.0.7559.256`, multi-process operation, and the legacy renderer sandbox without `--no-sandbox`.
- TLS 1.3 and secure loading of GitHub, Reddit, YouTube, and issuer-diverse HTTPS test sites.
- Downloads, PDF viewing, profiles, persistence, browser internal pages, WebGL/SwiftShader, and modern JavaScript/web-platform features.
- Extension install, permissions, enable/disable, request blocking, cosmetic filtering, restart persistence, and removal using uBlock Origin `1.73.0` (Manifest V2), AdBlock `6.44.0` (Manifest V3), and h264ify `2.0.1` (Manifest V3).
- Exact-release-ZIP YouTube playback on AV1/Opus for 57.50 seconds with 1,529 decoded frames, 9 dropped frames (0.59%), decoded audio bytes, buffered media, and no media error.
- Exact-release-ZIP YouTube playback through h264ify-compatible H.264/AAC selection for 57.96 seconds with 1,738 decoded frames, 7 dropped frames (0.40%), decoded audio bytes, buffered media, and no media error.
- YouTube pause, resume, seeking, volume state, and tab closure.
- Both native self-extractors on untouched Windows 2000 SP4: 316 browser files and 133 certificate-package files extracted successfully.

YouTube and other modern services change continuously. The measurements above document this exact build and test date; they are not a permanent service guarantee. h264ify is compatible and recommended for slower hardware, but is not bundled.

## Diagnostics are strictly opt-in

Normal launches create no diagnostic preference, log, or `Diagnostic Logs` directory. Diagnostics run only when the user explicitly starts `Supermium with Diagnostics.cmd` and confirms the full disclosure.

An accepted session creates readable text/JSON Lines beside the launcher, never uploads anything, uses a new temporary browser profile, and deletes that profile after the browser exits. The user can review, edit, delete, keep, or decline to share every file.

The collector excludes usernames, computer names, IP/MAC addresses, URLs, YouTube titles and IDs, accounts, cookies, passwords, form contents, history, personal-file information, screenshots, recordings, and crash dumps. The exact final-package test produced 66 valid media records, passed the privacy scan, and completed temporary-profile cleanup. See [docs/DIAGNOSTICS.md](docs/DIAGNOSTICS.md).

## Known limits

- Audible YouTube output needs bare-metal Windows 2000 confirmation.
- WebGPU did not expose an adapter in the validated 32-bit VM.
- Commercial Widevine DRM playback, including Netflix, is not claimed.
- A 32-bit process and operating system remain constrained by their address space. Testing found 1.75 GB RAM and one CPU to be a lower functional YouTube boundary, but cold startup there was unreliable; 2 GB/two CPUs is the supported recommendation.
- Windows 2000 is unsupported and no longer receives Microsoft security fixes. Running a modern browser does not make the operating system safe.

## Source and licensing

Large compiled browser artifacts are kept in GitHub Releases, not Git history. This repository is the exact version-locked compatibility and packaging source applied to the stated upstream release. The patch scripts refuse unexpected input bytes or hashes; offsets must be re-audited for every future Supermium version.

Supermium and Chromium retain the BSD 3-Clause license in [LICENSE](LICENSE). Mozilla certificate data retains the Mozilla Public License 2.0. All upstream copyrights, licenses, notices, and attribution remain in place. This project is independent and is not an official `win32ss` release. See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and [docs/BUILDING.md](docs/BUILDING.md).
