WINDOWS 2000 MODERN ROOT CERTIFICATE INSTALLER
================================================

Purpose
-------
This package adds a current set of public TLS server-authentication root
certificates to the Windows 2000 Local Computer "Trusted Root Certification
Authorities" store. Windows 2000 does not include certutil.exe or certmgr.exe,
so W2KROOTS.EXE performs the same store operation through the native
crypt32.dll API that ships with Windows 2000.

The package contains 121 roots extracted from Mozilla's certificate program.
See CERTIFICATE-LIST.txt for every subject, validity period, SHA-1 fingerprint,
SHA-256 fingerprint, and .cer filename. CERTIFICATE-MANIFEST.json contains the
same information in machine-readable form.

Package formats
---------------
The certificate installer is distributed in two equivalent formats:

- Certificates-Installer-Windows-2000.zip for systems with a ZIP extractor.
- Certificates-Installer-Windows-2000.exe, a native Windows 2000
  self-extracting package for an unmodified installation that has no built-in
  ZIP support. The EXE only extracts files; it does not install certificates or
  make any system change by itself. Choose a destination, then continue with
  INSTALL.cmd as described below.

Automatic installation
----------------------
1. Sign in using an Administrator account.
2. Extract the entire ZIP, or run the self-extracting EXE and choose a
   destination. Keep INSTALL.cmd, W2KROOTS.EXE, and the certs directory together
   in the extracted package; INSTALL.cmd expects that layout.
3. From the extracted package, double-click INSTALL.cmd.
4. Confirm the final summary says files=121, present=121, failed=0.
5. Fully close and restart Supermium. A Windows reboot is recommended for
   other applications that cache certificate-store data.
6. Run VERIFY.cmd at any time to check all included certificates.

Source and integrity
--------------------
Download page:
  https://curl.se/docs/caextract.html

Direct bundle URL:
  https://curl.se/ca/cacert.pem

Mozilla source referenced by the downloaded bundle:
  https://raw.githubusercontent.com/mozilla-firefox/firefox/refs/heads/release/security/nss/lib/ckfw/builtins/certdata.txt

Bundle date: 2026-08-13 03:12:01 GMT
Bundle file SHA-256:
  F66DFF1BDF8F96060B8177976F8B7D9254BC89BC4DB933D769F7384D28480BC9

The original downloaded file is included as cacert.pem. The curl CA Extract
service converts Mozilla's server-authentication trust anchors to PEM and
checks Mozilla's source daily. The certificate data is distributed under the
Mozilla Public License 2.0; see LICENSE-MPL-2.0.txt.

Manual installation
-------------------
Windows 2000's graphical Certificates snap-in can import the .cer files one at
a time:

1. Select Start, Run, type mmc.exe, and press Enter.
2. In MMC, choose Console, Add/Remove Snap-in, then Add.
3. Select Certificates and choose Add.
4. Select Computer account, then Local computer, and finish the wizard.
5. Expand Certificates (Local Computer), then Trusted Root Certification
   Authorities, then Certificates.
6. Right-click Certificates and choose All Tasks, Import.
7. Select a file from the certs directory, complete the wizard, and repeat for
   each certificate you intentionally want to trust.
8. Close and restart Supermium after changing the trust store.

Important security notes
------------------------
- Root certificates are trust anchors. Install only a package whose hashes and
  source you have verified.
- The curl PEM conversion carries the CA certificates, but Mozilla-specific
  external name constraints are not represented by the PEM format. This is a
  documented limitation on the curl CA Extract page. Windows 2000 also cannot
  reproduce all modern browser root-program policy metadata.
- Supermium includes Chromium's Chrome Root Store for normal public website
  validation. Chromium also considers locally managed Windows roots, which is
  why this installer remains useful on Windows 2000 and for private CAs. A
  controlled test confirmed that a Windows 2000 machine root is honored after
  Supermium restarts.
- Installing roots cannot add modern TLS protocol or cipher support to old
  Windows applications. Supermium supplies its own modern TLS stack; legacy
  WinINet/Schannel applications may still fail for protocol reasons.
- There is no automatic uninstall because deleting an included root could also
  delete a certificate that was trusted before this package was installed.
  For rollback, use a VM snapshot/system backup or manually remove only roots
  you have confirmed were added by this package.

Tested result
-------------
On Windows 2000 SP4, the native Local Computer root-store count changed from
191 to 312. All 121 files were installed and verified, all remained present
after reboot, and Supermium completed secure TLS 1.3 connections through
multiple public issuers, including the Let's Encrypt ISRG Root X1 and X2 test
sites. A private controlled root changed its test page from
ERR_CERT_AUTHORITY_INVALID to secure after a restart; removing that root and
restarting restored the expected rejection.

Included program
----------------
W2KROOTS.EXE is a 32-bit native Win32 console program. It dynamically calls
CertOpenStore, CertAddEncodedCertificateToStore, CertEnumCertificatesInStore,
and CertCloseStore from the Windows 2000 crypt32.dll. Its source is included in
the source directory.
