# Explicit opt-in diagnostics

## Normal mode

Normal launches never create a diagnostic preference, log, temporary diagnostic profile, or `Diagnostic Logs` directory. There is no telemetry upload path.

## Starting one diagnostic session

Run `Supermium with Diagnostics.cmd`. Before any log directory is created, the launcher displays a disclosure explaining:

- the exact categories that may be recorded;
- the categories deliberately excluded;
- that a new temporary browser profile will be used;
- that nothing is uploaded;
- where the files will be stored;
- that users should review, edit, or delete files before sharing;
- that YouTube and bare-metal audio testing are the principal Release Candidate 1 focus.

Canceling the dialog returns without creating diagnostic output. Confirming creates a timestamped session under `Diagnostic Logs` beside the launcher.

## Files and approved fields

- `session-summary.txt`: build, UTC times, OS version/service pack, logical processor count, processor architecture code, physical-memory amount, display dimensions/color depth, exit code, runtime, monitor status, and temporary-profile cleanup result.
- `system-hardware.txt`: OS edition/build/language, memory amount, generic manufacturer/model, CPU model/clock/cache/status, display adapter/driver/mode/status, sound device/driver status, and installed hotfix identifiers when WMI exposes them.
- `browser-technical.log`: sanitized fixed technical event categories derived in memory from Chromium logging.
- `youtube-media.jsonl`: fixed media state fields such as dimensions, time, duration, pause/seek/mute/volume state, buffered/played ranges, decoded/dropped/corrupt frame counters, decoded audio/video byte counters, ready/network state, stall time, and broad page kind (`watch` or `other`).
- `COLLECTED-DATA.txt`, `REVIEW-BEFORE-SHARING.txt`, and `TESTING-AND-REPORTING.txt`: the disclosure and review instructions copied into the session.

## Explicit exclusions

The design excludes usernames, computer names, serial numbers, product keys, IP/MAC addresses, network configuration, full URLs, YouTube titles and video IDs, account data, cookies, passwords, form contents, history, bookmarks, personal-file paths/content, screenshots, recordings, and crash dumps.

Raw Chromium text is not copied to the report. The launcher reduces recognized log lines to fixed categories and discards the raw text in memory. The YouTube extension sends only the fixed technical payload to a loopback-only session monitor.

## Profile isolation and cleanup

Every diagnostic session creates a new private temporary profile inside that session. It does not reuse the user's normal profile. After the browser exits, cleanup retries remove the temporary profile and record the result in `session-summary.txt`.

Do not sign into any account or enter private data during a diagnostic session even though the collector is designed not to record it.

## Final validation

The exact packaged launcher was tested in explicit diagnostic mode. The monitor was available, YouTube progressed for 19.298903 seconds with 579 decoded frames and 5 dropped frames, decoded audio bytes were observed, 12 media responses completed, and no media error occurred. The final session contained 66 valid JSON Lines records, browser exit code 0, and `temporary_private_profile_cleanup=complete`.

The privacy audit found no username, email address, IP address, MAC address, video ID, or visited browsing URL. The only URL present was the intentional static public GitHub Issues address in the reporting guide.
