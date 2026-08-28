SUPERMIUM WINDOWS 2000 Release Candidate 1 DIAGNOSTICS
======================================

Diagnostics are explicitly opt-in per session. Normal Supermium launches never
create a project diagnostic preference, project diagnostic log, or "Diagnostic
Logs" folder. Nothing is uploaded automatically.

To run one diagnostic session, use:

- Supermium with Diagnostics.cmd

The launcher first displays a field-level privacy summary. No diagnostic folder
is created unless you choose Yes. The session uses an isolated temporary browser
profile and removes it after the browser closes.

Useful package shortcuts:

- Review Diagnostic Logs.cmd
- View Diagnostic Data Disclosure.cmd
- Testing and GitHub Issue Guide.cmd

After a diagnostic session closes, the launcher offers to open the exact session
folder. Read every file first. You may edit, delete, keep, or decline to share
anything.

YouTube testing is strongly recommended. During playback, click the diagnostic
extension icon and record one of the fixed result markers. The monitor records
technical media state but never the URL, video title, video ID, channel, search
terms, or account.

Bare-metal YouTube confirmation is the primary reason for this Release Candidate 1 and the
diagnostic mode. Development had no bare-metal Windows 2000 machine; virtual
machines cannot conclusively validate real audio, graphics, timing, or drivers.

See COLLECTED-DATA.txt for the complete field-level disclosure.
See TESTING-AND-REPORTING.txt for the test checklist and GitHub Issues link.
