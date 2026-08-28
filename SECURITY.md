# Security policy

Windows 2000 is unsupported and no longer receives Microsoft security updates. This project improves application compatibility; it cannot make the operating system secure. Use isolation, backups, a firewall, least-privilege accounts, and no sensitive personal data.

The browser must be started without `--no-sandbox`. The supplied launcher adds the validated `--legacy-sandbox` path on Windows 2000.

Root certificates are trust anchors. Verify release SHA-256 values, obtain the certificate bundle from the documented official sources, review the included list, and install only roots you intend to trust.

Diagnostics are local and explicitly opt-in. Nothing is uploaded automatically. Review and redact every diagnostic file before attaching it to an issue. Never post passwords, cookies, account data, private URLs, product keys, personal files, IP/MAC addresses, usernames, or computer names.

For a suspected vulnerability, open a minimal issue without exploit details or personal data and request a private contact route. Do not publish a weaponized proof of concept against users of an unsupported operating system.

