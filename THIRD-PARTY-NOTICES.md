# Third-party notices

## Supermium and Chromium

This work is based on [win32ss/supermium](https://github.com/win32ss/supermium), itself a Chromium fork. The validated upstream tag is `v144-r5` at commit `0cdb6d9aa53c96875f637f7303115fb8fdd9d502`.

The repository and binary package retain the Chromium/Supermium BSD 3-Clause license and upstream copyright notices. Bundled browser components retain their own notices and licenses in the upstream distribution.

## Mozilla root certificate data and curl CA Extract

The certificate package uses the Mozilla CA certificate program data converted to PEM by [curl's CA Extract service](https://curl.se/docs/caextract.html). The downloaded source bundle is `https://curl.se/ca/cacert.pem`, dated 2026-08-13 03:12:01 GMT with SHA-256 `F66DFF1BDF8F96060B8177976F8B7D9254BC89BC4DB933D769F7384D28480BC9`.

Mozilla certificate data is distributed under the Mozilla Public License 2.0. The full MPL 2.0 text is retained at `certificates/LICENSE-MPL-2.0.txt`. The curl conversion's documented limitation applies: Mozilla-specific external name constraints are not represented in the PEM output.

## Legacy Update

[Legacy Update](https://legacyupdate.net/) is recommended but not redistributed or modified here. It is a separate community project and is not affiliated with Microsoft.

## Extensions

uBlock Origin, AdBlock, and h264ify were used only as compatibility test subjects. They are not bundled in this repository or release. Their names and licenses belong to their respective projects.

## Independence

This Windows 2000 compatibility project is independent. It is not an official release of win32ss, Google, Mozilla, curl, Legacy Update, Microsoft, or any tested extension project.
