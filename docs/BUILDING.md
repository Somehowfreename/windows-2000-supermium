# Reproducing the Windows 2000 payload

## Version-locked input

- Project: `win32ss/supermium`
- Tag: `v144-r5`
- Commit: `0cdb6d9aa53c96875f637f7303115fb8fdd9d502`
- Portable package: `supermium_144_32_nonsetup.zip`
- Input SHA-256: `17ACFCDF89EA651905053B50B0FCE5A28DB19CB2C69ED5579C7B177806ED6D31`

Do not bypass any input-hash or expected-byte failure. These patches target one binary build. A different Supermium update requires a source/assembly audit, new offsets and expected bytes, then the complete test matrix.

## Tools

- Python 3 with `pefile`.
- Node.js for manifests and SFX packaging.
- A 32-bit C compiler that can produce Windows 2000-compatible PE files. The release bridge DLLs and SFX stub were built with TinyCC 0.9.27.
- Microsoft `makecab.exe` for the CAB payload used by the self-extractor.
- An archive tool capable of creating a normal ZIP without altering names or file contents.

Compilers are not vendored in this repository. Do not commit build output; compare it to the expected hashes below and place final binary packages in GitHub Releases.

## Native bridges

Build these sources as 32-bit DLLs using the paired DEF file:

| Output in package | Source | DEF | Expected SHA-256 |
|---|---|---|---|
| `p_s232.dll` | `compat/w2k_s232.c` | `compat/p_s232-w2k-raw.def` plus reference-export rebuild | `6A2A33053D2E0EFCBB6D031AD68F6BD0B417B2D418A625BC193AFE5C756BB01D` |
| `p_wtsapi.dll` | `compat/w2k_wts.c` | `compat/p_wtsapi.def` plus export-name normalization | `418E34C1122C226ED62EDE8685E03908B4AF6C7227A9DB9E35614276964961C7` |
| `w2kjob2.dll` | `compat/w2k_job.c` | `compat/w2kjob2.def` | `C6D278D399A03C71DFD5B701C6DFB37DA412ED0186264A69BAD458627A5CC2ED` |
| `w2ksync.dll` | `compat/w2k_sync.c` | `compat/w2ksync.def` | `F1EDAC1B7975E8C44067B33C91112FA4999340424078DDBE4E163E25677116AC` |
| `w2kveh.dll` | `compat/w2k_veh.c` | `compat/w2kveh-candidate10.def` | `6F547006ED4BE7FCE6F47739FE4D2002541071B7C2F84785BCEC4CBDF76E0693` |

The Winsock bridge must preserve the ordinals expected by upstream `p_s232.dll`. First compile the raw bridge, then run:

```text
python scripts/rebuild_pe_exports_from_reference.py out/p_s232-w2k-raw.dll upstream/p_s232.dll out/p_s232.dll --dll-name p_s232.dll --manifest out/p_s232-exports.json
python scripts/validate_import_contract.py upstream/chrome.dll p_s232.dll out/p_s232.dll
```

`manifests/p_s232-w2k-exports.json` and `manifests/p_s232-w2k-chrome-import-contract.json` record the exact release result.

TinyCC initially emits decorated stdcall export names for the WTS bridge. Normalize the four names after compilation:

```text
python scripts/normalize_pe_exports.py out/p_wtsapi.dll _WTSFreeMemory@4=WTSFreeMemory _WTSQuerySessionInformationW@20=WTSQuerySessionInformationW _WTSRegisterSessionNotification@8=WTSRegisterSessionNotification _WTSUnRegisterSessionNotification@4=WTSUnRegisterSessionNotification --manifest out/p-wtsapi-export-normalization.json
```

The checked release result is retained in `manifests/p-wtsapi-export-normalization.json`.

## Final patch chain

Apply the Python patchers in this order to a fresh copy of the verified upstream portable package:

1. `patch_w2k_pe.py` — lowers PE OS/subsystem declarations and redirects the required context capture import.
2. `patch_p_cryptp_w2k.py` — translates the unavailable CryptoAPI provider type used by `ProcessPrng`.
3. `patch_pwrp_getnativesysteminfo_w2k.py`.
4. `patch_pwrp_w2k_candidate4.py`.
5. `patch_pwrp_w2k_candidate5.py`.
6. `patch_pwrp_w2k_candidate6.py`.
7. `patch_pwrp_w2k_candidate7.py`.
8. `patch_pwrp_w2k_candidate8.py`.
9. `patch_chrome_w2k_candidate9.py` — routes delayed WTS imports to `p_wtsapi.dll`.
10. Install the Candidate 10 `w2kveh.dll` bridge listed above.
11. `patch_pwrp_w2k_sync_candidate11.py`.
12. `patch_candidate12_job_and_pdh.py`, followed by `route_candidate12_job_to_fresh_module.py`.
13. `patch_candidate13_thread_priority_w2k.py`.
14. `patch_candidate14_network_service_in_process_w2k.py`.
15. `patch_candidate17_ole_initialize_spy.py`.
16. `patch_candidate18_job_kill_on_close_w2k.py`.
17. `patch_candidate19_w2k_ntdll_interceptions.py`.

The corresponding production manifests are in `manifests/`. Candidate 15 introduced no binary change. Candidate 16 was diagnostic-only, was rejected for the release, and is deliberately absent. Candidate 20 was an experiment after the finalized Candidate 19 payload and is deliberately absent.

The final browser-specific outputs include:

| File | SHA-256 |
|---|---|
| `chrome.exe` | `52CC9AE71BD5A847ED26D676BABB43F6308C62E0EBEEFD19363DADBCF8F2C38B` |
| `pwrp_k32.dll` | `D5816017C0603D7C9B445EB47C338337D7A64963457B0ED360857B97C812059C` |
| `p_cryptp.dll` | `4611005457D32E945F5AD0CC0037F0A269ECB882DF879798C8AFE5382B654298` |
| `p_ole.dll` | `926206CE0C5D8F78111095A9A75989D308F24C2860AB6553DD386A9E83553C23` |

## Diagnostics launcher

Compile `diagnostics/supermium_w2k_rc1_launcher.c` as a 32-bit Windows GUI executable and retain the adjacent `diagnostics` data tree and package launchers. The exact release launcher is 25,600 bytes with SHA-256 `E72DF51E05DE98F5F309FF2307923026A8E0EC3A57491920EADB41DA04D3B543`.

Run `node diagnostics/tests/test_extension_privacy.js` and the launcher's built-in sanitizer self-test before packaging.

## Certificate package

`certificates/cacert.pem` is the exact curl/Mozilla input. `certificates/source/split_mozilla_ca_bundle.js` produces the 121 DER `.cer` files and machine-readable manifest. Compile `certificates/source/w2kroots.c` as a 32-bit Windows console program named `W2KROOTS.EXE`. Verify every entry in `certificates/CHECKSUMS.sha256` before packaging.

## Self-extractors

Compile `sfx/w2k_sfx.c` as a 32-bit Windows GUI executable named `w2k-sfx-stub.exe`. Then run:

```text
node sfx/build_w2k_sfx.mjs w2k-sfx-stub.exe <input-directory> <output.exe> <folder-name> <display-name>
```

The builder creates an LZX CAB, appends it to the native stub, and writes a fixed footer containing the CAB size, CRC-32, destination folder name, and display name. The stub validates the CRC, rejects absolute/traversal paths, lets the user choose a parent directory, and extracts through the Windows 2000 `SetupIterateCabinetA` API.

Independently expand every embedded CAB and compare every relative path, byte count, and SHA-256 with its source tree. The final SFXs additionally passed live extraction on untouched Windows 2000 SP4.

## Final packaging gate

1. Regenerate the browser `SHA256SUMS.txt` and verify every entry after a clean extraction.
2. Confirm a normal launch produces no diagnostic directory.
3. Run explicit diagnostics and verify its disclosure, privacy allowlist, monitor, browser exit, and temporary-profile cleanup.
4. Repeat sandbox, TLS, site, YouTube default-codec, YouTube H.264/AAC, interaction, extension, profile, PDF, download, and WebGL tests.
5. Hash all release assets and compare them with `manifests/final-release.json`.
