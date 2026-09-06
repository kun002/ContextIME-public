# M5.5 User-Approved Terms Host Verification

## Conclusion

The M5.5 manual-term path was exercised on the real, logged-in Windows 11
desktop with the installed `0.5.4-preview` package (in-place upgraded from
0.5.3, reboot completed by the user). The management path is fully verified:

```text
0.5.4 manager 添加术语 (real UI click)
        → bounded CIPM UPSERT_TERM
        → installed 0.5.4 Context Service Project Indexer owner
        → persisted term/manual record with server-owned last_seen
        → manager view refresh
0.5.4 manager 删除词条 (real UI click)
        → bounded CIPM REMOVE_ENTRY
        → exact-key single-entry removal
        → real dictionary restored byte-identical (SHA-256 verified)
```

The remaining open step is narrower than before: with the ContextIME profile
active and the 0.5.4 server freshly restarted, typing `ceshishuyu` in the
target editor and committing `CeShiShuYu 〔项目·术语〕` still needs one
interactive observation. The first attempts were invalidated by a dead
ContextIME TSF server (fail-open English passthrough mistaken for
composition), which was diagnosed and fixed during the session.

```text
M5.5_MANAGEMENT_PATH_INSTALLED = VERIFIED
M5.5_INSTALLED_CANDIDATE_ACCEPTANCE = REAL_WINDOWS_VERIFICATION_REQUIRED
```

## Fixed product and build

| Item | Value |
|---|---|
| Product | `ContextIME 0.5.4-preview` (installed, in-place over 0.5.3) |
| Installer bytes | `12,299,493` |
| Installer SHA-256 | `eed498b66b00cb4932df36cd09005f559cdfafe53802a067ea57d5dee004e37b` |
| Context Service SHA-256 | `212c0ebd00a1d2a042fdf02ffc230767ecfa9b52b9eb5b5bb7eee79520e71d2e` |
| Manager SHA-256 | `b7fd0cb333d32e66ce16e941888ff0624dfd4451b2e85f90a17eafa1c2fd88ee` |
| Installer CI run | [34008448679](https://github.com/kun002/ContextIME-public/actions/runs/34008448679) |
| Weasel commit | `9cc96e20dc71b80876b12f689bb5863c76c2a7ed` |
| librime commit | `1c23358157934bd6e6d6981f0c0164f05393b497` |
| OS | Windows 11 Professional x64, build `26200.9168` |

All three hashes were recomputed locally from the downloaded artifact and
matched the generated manifest. The user completed the in-place upgrade and
rebooted; the `0.5.3-preview` install leaf was replaced by
`contextime-0.5.4-preview` and the autorun services came back healthy.

## Verified on the real desktop

1. **Installed 0.5.4 service lifecycle after reboot.** The installed
   Context Service owns `ContextIME.ContextService.v1`,
   `ContextIME.ProjectIndexer.v1`, and `ContextIME.ProjectCandidate.v1`
   after the reboot; the TSF input pipe returns once the server starts.
2. **Real Adapter ingestion into installed 0.5.4.** A real VS Code window
   with the report-only `0.5.0` adapter re-created project dictionary
   `12646ea4563df944ed5c60858d006cf7.dict` (same opaque ID for the same
   workspace path, confirming path-domain determinism) with
   `AcceptanceController` (class) and `runAcceptance` (method).
3. **Manager add-term (M5.5 core).** A real 添加术语 click with `CeShiShuYu`
   produced the persisted record
   `term 1 1788671209448 manual 43655368695368755975` with server-owned
   `last_seen`; prior `language_server` records stayed byte-identical.
4. **Manager single-entry removal (M5.5).** After the term was accidentally
   added to the real project dictionary `e37d5123…` (manager auto-selection
   race during the test), the 删除词条 flow with its confirmation removed
   exactly the `term/manual CeShiShuYu` key. The real dictionary returned to
   SHA-256 `5c9441309c389d311a300613c1234578634fc405955b31555e820dfda06da3cd`
   — byte-identical to its pre-test state, proving add and remove are exact
   inverses on persisted data.
5. **M5.4 regression (staged pass).** The confirmed whole-library delete was
   exercised earlier in the session against the staged 0.5.4 binaries and
   cleared the test project as expected.
6. **Root cause of the earlier "no candidates" observations.** The
   ContextIME TSF server process had died during earlier smoke timeouts; the
   TSF fail-open path then produced raw English passthrough whose VS Code
   spell-check underline was mistaken for IME composition. After restarting
   the installed 0.5.4 server and letting librime deploy complete, the TSF
   smoke confirms the ContextIME profile activates
   (`ProfileActivationHResult = 0x0`).

## Not completed in this session

- **Candidate commit observation.** Typing `ceshishuyu` under the active
  ContextIME profile with the workspace lease live and committing
  `CeShiShuYu 〔项目·术语〕` still requires one interactive desktop check.
  The session was interrupted twice for safety: first because the user moved
  into a full-screen RDP session (synthetic keys could reach the remote
  machine), then again for the same reason. The manual-term record is
  already persisted in the target project dictionary, so the remaining step
  is a single typing observation on a free desktop.

  Additional diagnoses collected for that final pass:

  - The driver tooling must send virtual-key events (the app-smoke
    `TypeAscii` approach); `KEYEVENTF_UNICODE`-style text injection bypasses
    the IME pipeline entirely and always produces raw text, which invalidated
    several early observations.
  - The TSF smoke confirms ContextIME composition, candidates, and commit
    work end-to-end in its own window (`shurufa → 输入法`, `Passed = true`)
    after the server restart, so the server-side pipeline is healthy.
  - VS Code typing stayed raw even with virtual-key events, including inside
    a real `//` comment line. The editor-context decision mapping
    (`Comment → Chinese`) exists in the Context Service, but the M3
    "real TSF automatic switching" loop has never been verified on a real
    desktop (`REAL_WINDOWS_VERIFICATION_REQUIRED` from M3), so the comment
    context may not be applied to the live session yet. The next pass should
    either confirm the automatic comment switch or force Chinese mode
    manually before typing.
- **Installer reboot behavior.** The NSIS script sets `SetRebootFlag true`
  unconditionally on the upgrade path, which forced the reboot the user
  experienced after installing 0.5.4. Recorded as a 0.5.5 installer task:
  replace the forced reboot with an explicit prompt, and evaluate whether a
  reboot can be avoided entirely for same-layout upgrades (the versioned
  install leaf means the new files never collide with running binaries).
