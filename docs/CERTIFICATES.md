# Modern root certificates on Windows 2000

## Why the release includes a certificate package

Windows 2000's machine root store predates most current public certificate authorities. Supermium carries Chromium's Chrome Root Store for ordinary public validation and can also honor locally managed Windows roots. Updating the machine store is useful for locally managed/private authorities and for other Windows components, although it cannot add modern TLS protocol support to legacy Schannel/WinINet applications.

## Equivalent release formats

- `Certificates-Installer-Windows-2000.zip` contains the files directly.
- `Certificates-Installer-Windows-2000.exe` is a native Windows 2000 self-extractor for a system without ZIP support.

The EXE only extracts files. It does not install a certificate or change the system until the user explicitly runs `INSTALL.cmd` from the extracted directory.

Keep `INSTALL.cmd`, `VERIFY.cmd`, `W2KROOTS.EXE`, and the `certs` directory together. `INSTALL.cmd` resolves its inputs relative to its own directory.

## Automatic installation

1. Sign in with an Administrator account.
2. Extract either package completely.
3. Review `README.txt`, `CERTIFICATE-LIST.txt`, `CHECKSUMS.sha256`, and the source URLs.
4. Run `INSTALL.cmd`.
5. Confirm `files=121, present=121, failed=0`.
6. Run `VERIFY.cmd` at any later time.
7. Fully close and restart Supermium. Rebooting Windows is recommended for other applications that cache trust data.

`W2KROOTS.EXE` is a small open-source native Win32 console tool. It dynamically calls the Windows 2000 `crypt32.dll` certificate-store APIs; it does not require `certutil.exe` or `certmgr.exe`, neither of which is provided by a normal Windows 2000 installation.

## Official sources and integrity

- curl CA Extract information: https://curl.se/docs/caextract.html
- Direct bundle: https://curl.se/ca/cacert.pem
- Mozilla source referenced by the bundle: https://raw.githubusercontent.com/mozilla-firefox/firefox/refs/heads/release/security/nss/lib/ckfw/builtins/certdata.txt

The included source bundle is dated 2026-08-13 03:12:01 GMT. Its SHA-256 is:

```text
F66DFF1BDF8F96060B8177976F8B7D9254BC89BC4DB933D769F7384D28480BC9
```

The bundle, all generated `.cer` files, source code, manifest, list, license, and checksums are retained under `certificates/` so the release can be independently audited and reproduced.

## Manual installation

Users who do not want to run the batch file can import selected `.cer` files through the native MMC snap-in:

1. Select Start, Run, enter `mmc.exe`, and press Enter.
2. Choose Console, Add/Remove Snap-in, then Add.
3. Select Certificates and choose Add.
4. Select Computer account, then Local computer.
5. Expand Certificates (Local Computer), Trusted Root Certification Authorities, Certificates.
6. Right-click Certificates, select All Tasks, Import.
7. Select one file from the `certs` directory and complete the wizard.
8. Repeat only for each root you intentionally trust, then restart Supermium.

## Security limitations

- Root certificates are trust anchors. Verify hashes and install only material obtained from a source you trust.
- Mozilla-specific external name constraints are not represented in the curl PEM conversion. This is documented by curl and cannot be recreated fully by the Windows 2000 certificate store.
- There is no automatic uninstall because removing an included root could delete trust that existed before this package was used. Use a snapshot/backup or manually remove only roots you have confirmed were added by this package.
- Installing roots cannot repair obsolete TLS/cipher support in other old applications. Supermium supplies its own modern TLS implementation.

## Validation result

On the official Windows 2000 SP4 validation machine, the Local Computer root count changed from 191 to 312. All 121 roots installed, verified, and remained present after reboot. Supermium completed TLS 1.3 connections through several public issuers. A controlled private root changed a test endpoint from `ERR_CERT_AUTHORITY_INVALID` to secure after browser restart, and removing the root restored rejection.

