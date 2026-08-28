# Final validation record

## Test environments

1. Official English Windows 2000 Professional SP4 installed from an original Microsoft ISO, then fully updated through Legacy Update. VirtualBox Guest Additions 6.0.24 supplied only VM display/input integration; no extended kernel or One-Core-API was installed.
2. An untouched snapshot of the same official SP4 installation, with no post-SP4 updates and no Guest Additions, used only to prove both native self-extractors work on a bare system.
3. Earlier clean-validation Windows 2000 VMs used for low-resource, certificate-store, extension, sandbox, native-audio, graphics, and restart-persistence investigations.

The exact browser tree was extracted from `Supermium-144.0.7559.256-R5-Windows-2000-RC1-x86.zip`, and its 315-entry internal SHA-256 manifest was checked before dynamic tests.

## Release gates

| Area | Result |
|---|---|
| Native launch | Passed; Chrome `144.0.7559.256` |
| Sandbox | Passed with `--legacy-sandbox`; no `--no-sandbox` |
| Normal-mode logging | Passed; no diagnostic preference or `Diagnostic Logs` directory created |
| TLS/site matrix | GitHub, Reddit, YouTube, example.com and issuer-diverse test endpoints loaded securely; TLS 1.3 observed |
| Web platform | Modern JavaScript, storage, workers, fetch, secure context and tested platform probes passed |
| Browser features | Downloads, PDF, profiles, persistence and internal pages passed |
| Graphics | Native GPU and SwiftShader WebGL paths passed; WebGPU not claimed |
| Extensions | uBlock Origin, AdBlock and h264ify installation, permissions, lifecycle and restart persistence passed |
| Diagnostics | Explicit opt-in, disclosure, loopback monitor, allowlist, privacy scan, exit and cleanup passed |
| Certificate package | 121 roots installed/verified/persisted; public and controlled-private-root tests passed |
| Browser SFX | 316 files extracted on untouched SP4 |
| Certificate SFX | 133 files extracted on untouched SP4 |

## Exact-ZIP YouTube results

### Default codec path

- Selected codec: AV1/Opus (`av01.0.04M.08 (397) / opus (251)`).
- Elapsed media progression: 57.504911 seconds.
- Decoded frames: 1,529.
- Dropped frames: 9 (0.58862%).
- Resolution: 854×480.
- Approximate buffered media: 92.5 seconds.
- Decoded audio bytes observed: yes (1,117,038 in the final statistics sample).
- Media responses: 24.
- Media error: none.

### h264ify-compatible path

- Extension: h264ify `2.0.1`, Manifest V3, command-line loaded for the isolated test profile.
- Capability filter: H.264 allowed; VP8, VP9, AV1 and Opus filtered as configured.
- Selected codec: H.264/AAC (`avc1.4d401f (135) / mp4a.40.2 (140)`).
- Elapsed media progression: 57.958764 seconds.
- Decoded frames: 1,738.
- Dropped frames: 7 (0.40276%).
- Resolution: 854×480.
- Approximate buffered media: 92.88 seconds.
- Decoded audio bytes observed: yes (1,109,050 in the final statistics sample).
- Media responses: 29.
- Media error: none.

Pause, resume, seek, volume-state changes and tab closure all passed against the exact release package.

## Extension validation

- uBlock Origin `1.73.0` Manifest V2.
- AdBlock `6.44.0` Manifest V3.
- h264ify `2.0.1` Manifest V3.

Tests covered installation, extension identity/version, requested permissions, runtime listener registration, local request blocking, local cosmetic filtering, disabling, reenabling, restart persistence, and removal. Advertising websites were not used; controlled local fixtures provided deterministic request and cosmetic-filter targets.

## Audible-output limitation

The browser decoded audio data and advanced A/V media without media errors. VirtualBox repeatedly looped the first fraction of a native Windows system sound even when Supermium had never been launched. That makes its audible output unsuitable as evidence for or against browser correctness. Bare-metal testers should first verify an ordinary Windows sound, then test a public nonsensitive YouTube video for at least five minutes, including pause, resume, seeking, volume, fullscreen and tab closure.

## Repeating tests

The `tests/cdp` directory contains the DevTools Protocol probes used for browser, site, extension, media, codec, statistics and interaction checks. `tests/fixtures` contains local deterministic servers; `tests/native` contains Windows 2000 audio/inventory probe source. These tools do not contain personal profiles, credentials, test results, or captured user data.

Use a new disposable browser profile and a snapshot of a nonpersonal Windows 2000 test system. Never run diagnostics or test harnesses against a personal signed-in profile.
