# M5.5 User-Approved Terms Host Verification

## Conclusion

The M5.5 manual-term path was exercised on the real, logged-in Windows 11
desktop with the CI-built `0.5.4-preview` service and manager binaries
(staged, not installed). The complete management path passed:

```text
0.5.4 manager 添加术语 (real UI click)
        → bounded CIPM UPSERT_TERM
        → staged 0.5.4 Context Service Project Indexer owner
        → persisted term/manual record with server-owned last_seen
        → manager view refresh (2 → 3 entries)
```

A real VS Code report-only Adapter also ingested fresh Language Server
symbols into the same staged 0.5.4 service, and both original
`language_server` records stayed byte-identical. The unrelated real project
dictionary kept its exact SHA-256 across the entire session, and the test
project was removed afterwards through the manager's confirmed whole-library
delete.

```text
M5.5_STAGED_DESKTOP_MANAGEMENT = VERIFIED
M5.5_INSTALLED_CANDIDATE_ACCEPTANCE = REAL_WINDOWS_VERIFICATION_REQUIRED
```

## Fixed product and build

| Item | Value |
|---|---|
| Product | `ContextIME 0.5.4-preview` (CI-built, staged) |
| Installer bytes | `12,299,493` |
| Installer SHA-256 | `eed498b66b00cb4932df36cd09005f559cdfafe53802a067ea57d5dee004e37b` |
| Context Service SHA-256 | `212c0ebd00a1d2a042fdf02ffc230767ecfa9b52b9eb5b5bb7eee79520e71d2e` |
| Manager SHA-256 | `b7fd0cb333d32e66ce16e941888ff0624dfd4451b2e85f90a17eafa1c2fd88ee` |
| Installer CI run | [34008448679](https://github.com/kun002/ContextIME-public/actions/runs/34008448679) |
| Weasel commit | `9cc96e20dc71b80876b12f689bb5863c76c2a7ed` |
| librime commit | `1c23358157934bd6e6d6981f0c0164f05393b497` |
| OS | Windows 11 Professional x64, build `26200` |

All three hashes were recomputed locally from the downloaded artifact and
matched the generated manifest. The installer itself was not executed: the
elevation prompt was dismissed, so the verification ran the CI-built service
and manager binaries from a staged directory with unchanged ContextIME
identity (same pipes, mutexes, and data root).

## Verified on the real desktop

1. **Staged 0.5.4 service lifecycle.** After quitting the installed 0.5.3
   Context Service through its documented `--quit`, the staged 0.5.4 binary
   bound `ContextIME.ContextService.v1`, `ContextIME.ProjectIndexer.v1`, and
   `ContextIME.ProjectCandidate.v1` on the logged-in desktop.
2. **Real Adapter ingestion into 0.5.4.** A real VS Code window with the
   isolated report-only `0.5.0` VSIX and a fresh TypeScript workspace
   published `AcceptanceController` (class) and `runAcceptance` (method)
   into a new project dictionary `12646ea4563df944ed5c60858d006cf7.dict`
   through the staged 0.5.4 service.
3. **Manager add-term (M5.5 core).** The 0.5.4 manager listed the new
   project, and a real 添加术语 click with `CeShiShuYu` produced:
   - persisted record `term 1 1788666759213 manual 43655368695368755975`
     (UTF-8 `CeShiShuYu`) in the project dictionary;
   - both prior `language_server` records unchanged, including their
     original `last_seen` values;
   - manager view refreshed from `2 个词条` to `3 个词条` with the new row
     showing `term / manual / 1`, and the term input cleared.
4. **M5.4 regression.** The manager's confirmed whole-library delete still
   works on 0.5.4: the test project was removed after the confirmation
   dialog and the dictionaries directory returned to exactly the user's
   real dictionary.
5. **No disturbance to user data.** The real project dictionary
   `e37d5123336bb193cf26efc9ff1a31bc.dict` kept SHA-256
   `5c9441309c389d311a300613c1234578634fc405955b31555e820dfda06da3cd`
   before and after the entire session.
6. **Environment restored.** The staged service was quit, the installed
   0.5.3 Context Service restarted (all three pipes re-verified), the
   staged manager closed, and the ContextIME TSF server restarted.

## Not completed in this session

- **Installed candidate commit.** Typing `ceshishuyu` in a real editor and
  committing `CeShiShuYu 〔项目·术语〕` through the installed package remains
  open. The app-smoke driver could not pass its `/nascii` Chinese-mode
  handshake against the installed 0.5.3 server in this session state (the
  handshake process never exited within 5 seconds), and synthetic Shift /
  Ctrl+Space did not toggle IME modes for manual typing. This step needs the
  installed 0.5.4 package and a controlled interactive desktop session.
- **In-place 0.5.4 upgrade.** The verified installer was not executed
  because the UAC elevation prompt requires an interactive user; it can be
  installed directly to close the installed acceptance.
