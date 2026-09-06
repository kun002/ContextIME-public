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

The complete candidate path was then verified end to end on the installed
package: a fresh smoke workspace was ingested by the real Adapter, the
user-approved term was injected through the bounded `CIPM` `UPSERT_TERM`
op while the workspace lease was live, and the app-smoke observed the
candidate window and committed the term inside a TypeScript comment.

```text
M5.5_MANAGEMENT_PATH_INSTALLED = VERIFIED
M5.5_INSTALLED_CANDIDATE_ACCEPTANCE = VERIFIED
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

## Installed candidate acceptance (final pass)

The `vscode-project-candidate` app-smoke scenario was run against the
installed 0.5.4 package with `--input ceshishuyu --expected CeShiShuYu`:

```text
smoke workspace (isolated VS Code + report-only 0.5.0 adapter)
        → Language Server symbols ingested (b816-style fresh project)
        → CIPM UPSERT_TERM "CeShiShuYu" while the lease was live
        → persisted as term/manual with server-owned last_seen
        → smoke waits for the persisted symbol, then types in the comment
        → candidate window: 1. CeShiShuYu 〔项目·术语〕
        → space commits; editor ends with "// CeShiShuYu"
```

Evidence (artifacts of this session):

- Evidence JSON: `Passed = true`, `CandidateWindowDetected = true`
  (91,899 changed pixels), `ProjectCandidateCommitMatched = true`,
  final editor text ends with `// CeShiShuYu`;
- Candidate screenshot: the first candidate is
  `CeShiShuYu 〔项目·术语〕` followed by ordinary librime candidates
  (测试属于 / 测试 / 侧室 / 侧视);
- `m55-installed-acceptance2.json` and
  `m55-installed-acceptance2.json.project-candidate.png` under
  `artifacts/contextime-0.5.4-staged/`.

Two test-harness changes were required and are part of this change set:

- The app-smoke dictionary wait was hard-coded to
  `	language_server	<symbolHex>`; it now also accepts
  `	manual	<symbolHex>` so user-approved terms can drive the scenario.
- The term was delivered through a minimal CIPM client (the same bounded
  frame the manager uses) while the lease was live; the UPSERT_TERM publish
  path then propagated the snapshot to the candidate transport within the
  500 ms worker fetch.

All test workspaces and their dictionaries were removed afterwards through
CIPM `REMOVE_PROJECT`; only the user's real dictionary
(`e37d5123…`, SHA-256 unchanged) remains.

## Session diagnostics (recorded for future work)

- The ContextIME TSF server died repeatedly during this session. Each
  `/nascii` "hang" was actually a command child becoming a replacement
  server (the running instance was not found); the smoke's 5-second timeout
  then killed that fresh server. After the final clean start the server
  stayed healthy, but the instance-discovery failure behind the hang is
  worth investigating.
- `KEYEVENTF_UNICODE`-style text injection bypasses the IME pipeline
  entirely and always yields raw text; drivers must send virtual-key events
  (the app-smoke `TypeAscii` approach).
- VS Code typing stayed raw even with virtual keys while the editor context
  pipeline was not feeding decisions; the M3 "real TSF automatic switching"
  loop (comment → Chinese) has never been verified on a real desktop
  (`REAL_WINDOWS_VERIFICATION_REQUIRED` from M3). The M5.3-era manual force
  (`/nascii`) plus the manual-origin guard remains the working path.
- **Installer reboot behavior.** The NSIS script sets `SetRebootFlag true`
  unconditionally on the upgrade path, which forced the reboot the user
  experienced after installing 0.5.4. Recorded as a 0.5.5 installer task:
  replace the forced reboot with an explicit prompt, and evaluate whether a
  reboot can be avoided entirely for same-layout upgrades (the versioned
  install leaf means the new files never collide with running binaries).
