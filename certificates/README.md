# Certificate package source

This directory is the complete auditable source/data tree for the release certificate package, excluding only the compiled `W2KROOTS.EXE` binary that is distributed in GitHub Releases.

- `cacert.pem`: original curl/Mozilla bundle input.
- `certs/`: 121 generated DER certificate files.
- `CERTIFICATE-MANIFEST.json` and `CERTIFICATE-LIST.txt`: machine-readable and human-readable identity/fingerprint lists.
- `CHECKSUMS.sha256`: hashes for every package file except the checksum file itself.
- `source/split_mozilla_ca_bundle.js`: deterministic bundle splitter.
- `source/w2kroots.c`: native Windows 2000 machine-root installer/verifier source.
- `INSTALL.cmd` and `VERIFY.cmd`: relative-path package entry points.
- `README.txt`: documentation shipped inside both release formats.
- `LICENSE-MPL-2.0.txt` and `THIRD-PARTY-NOTICES.txt`: certificate-data licensing and attribution.

See `docs/CERTIFICATES.md` for sources, manual installation and security limitations.
