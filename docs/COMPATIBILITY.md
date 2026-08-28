# Windows 2000 compatibility architecture

## Supported target

- 32-bit Windows 2000 Professional or Server, Service Pack 4.
- All applicable post-SP4 updates installed through Legacy Update.
- No One-Core-API, extended kernel, or replacement Windows system DLLs.
- SSE2 processor.
- 2 GB RAM and two CPUs recommended.

The exact package is a 32-bit browser. The operating system and each process remain subject to 32-bit address-space limits.

## Compatibility layers

The release keeps changes local to the Supermium directory:

- PE target declarations are made acceptable to the Windows 2000 loader.
- Small native bridge DLLs supply or safely emulate the specific later-Windows calls required by this build.
- The Winsock bridge preserves upstream export names and ordinals while implementing Windows 2000-compatible `getaddrinfo`, `freeaddrinfo`, and `inet_ntop` paths.
- WTS registration calls become successful no-ops where Windows 2000 has no matching session-change facility; available WTS calls are forwarded to the system DLL.
- Vectored-exception, SID alias, `GetModuleHandleEx`, SRW/condition-variable, and job-query behavior is provided by focused native bridges.
- Browser-side patches avoid unavailable token-interception exports, preserve Windows 2000 scheduler behavior, correct OLE fallback stack cleanup, choose an available PDH counter, and avoid unsupported job-object flags while retaining the renderer sandbox.
- The Network Service runs in the browser process on Windows 2000 because the tested utility-process boundary repeatedly terminated on that OS.

Every production patch validates expected original bytes or a whole-file SHA-256 before mutation. The release does not use a generic “patch anything” mode.

## Sandbox

The supplied launcher adds `--legacy-sandbox`. Multi-process browser/renderer operation was inspected and tested without `--no-sandbox`. The unsupported Windows 2000 job-object kill-on-close flag is removed, but the sandbox job itself is retained.

## Graphics and media

Native GPU operation and a supplied SwiftShader launcher were tested. SwiftShader can help when a real Windows 2000 graphics driver cannot expose WebGL; Chromium marks that software path unsafe because it operates outside the GPU sandbox. WebGPU is not claimed.

YouTube selected AV1/Opus by default and H.264/AAC with h264ify compatibility. Both advanced for roughly 58 seconds with decoded audio bytes and low dropped-frame rates in the recommended VM. Audible bare-metal confirmation remains open because the VM's emulated audio repeated native Windows sounds independently of the browser.

## Resource boundary

Testing at 2 GB/two CPUs was dependable. Repeated h264ify-compatible playback remained functional at lower configurations; 1.75 GB/one CPU was the lowest tested boundary that could progress video, but cold startup was unreliable. One to 1.5 GB may handle lighter pages, but smooth modern video is not a supported expectation.

## Not claimed

- Widevine or Netflix playback.
- WebGPU.
- Compatibility with every future change to YouTube, Reddit, social networks, certificate programs, codecs, extensions, or DRM systems.
- Security of Windows 2000 itself.
