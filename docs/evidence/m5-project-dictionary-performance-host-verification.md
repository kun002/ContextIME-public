# M5 Project Dictionary Performance Host Verification

## Conclusion

ContextIME `0.5.1-preview` completed the installed-service large-project
pressure path on a real, logged-in and unlocked Windows desktop:

```text
100,000-entry isolated test dictionary
→ installed ContextIME.ProjectIndexer.v1
→ 20 x activate + 64-record incremental upsert
→ atomic persistence + active immutable snapshot publication

while:

Notepad → nihao composition/candidate → 你好 commit
        → English abc → Chinese mode restored
```

The 20 installed-service updates completed in 14,358 ms; the maximum measured
64-record update was 647 ms. Notepad passed every input assertion while that
pressure process was active. ContextIME Server, Context Service, and official
Weasel retained the same PID and start time across the run.

```text
M5_PROJECT_DICTIONARY_LARGE_PROJECT = VERIFIED
```

## Fixed product and host

| Item | Value |
|---|---|
| Product | `ContextIME 0.5.1-preview` |
| Product build commit | `3375dced2221b12d95d31d4a81b401fa6112ff28` |
| Installed pressure harness commit | `b16bce5c6924c69f36d88fb395be407c6580ca39` |
| Installer CI run | `33767999422` |
| Project Dictionary harness CI run | `33824760241` |
| Installer | `contextime-0.5.1-preview-installer.exe` |
| Installer bytes | `12,244,068` |
| Installer SHA-256 | `dab9eafbb389f2523ea06b37c9bb267b8af77e3af2ee469573b1f4ab883a9514` |
| Weasel commit | `9cc96e20dc71b80876b12f689bb5863c76c2a7ed` |
| librime commit | `1c23358157934bd6e6d6981f0c0164f05393b497` |
| Host | `WINDOWS_TEST_HOST` |
| OS | Windows 11 Professional, build `26200` |
| Interactive desktop session | `1` |

The CI artifact's size and SHA-256 matched its generated installer report.
The unsigned package performed an in-place upgrade from `0.5.0-preview`:
`contextime-0.5.1-preview` replaced the old install leaf, the old leaf was
removed, and official Weasel remained in `C:\Program Files\Rime`.

The existing read-only native evidence collector then reported:

```text
Expected state: Installed
machine registry: true
uninstall registry: true
install directory: true
TSF CLSID/TIP/language profile: true
system TSF DLL: true
server process and ContextIME native Pipe: true
user data directory: true
Mismatches: 0
```

## Installed Project Indexer pressure

The Windows `/W4 /WX` harness has no product-runtime role. It uses a fixed
opaque test project ID and supports three bounded operations:

1. with Context Service stopped, seed one 100,000-entry dictionary through the
   same `ProjectDictionaryStore` format;
2. with the installed service running, send only protocol-v1 activate/upsert
   requests to `\\.\pipe\ContextIME.ProjectIndexer.v1`;
3. after deactivation and a normal service stop, remove only that fixed test
   project.

It does not inspect a workspace, add a data source, or enter the key path. The
seed contained generated `PressureSymbol000000`-style identifiers only. It was
6,792,034 bytes and took 695 ms to create.

The installed service accepted 20 sequential batches while Notepad automation
was running:

```json
{"mode":"pressure","status":"OK","project_id":"11223344556677889900aabbccddeeff","entries":100000,"batch_size":64,"iterations":20,"elapsed_ms":14358,"maximum_update_ms":647}
```

Each iteration refreshed the active-project lease and then updated 64 existing
Language Server records. This exercised the installed service owner, real
Project Indexer Pipe, disk persistence, cached dictionary merge, and active
immutable snapshot publication—not merely the standalone Store fixture.

## Concurrent Notepad result

The existing application smoke tool ran in the same interactive desktop while
the 20 Pipe updates were in progress. It recorded:

```text
ProfileActivated: true
CandidateWindowDetected: true
CandidateChangedPixels: 33608
CommittedText: 你好
FinalText: 你好abc
CommitMatched: true
EnglishModeMatched: true
ChineseModeRestored: true
ProfileRestored: true
Passed: true
Errors: []
```

This verifies normal composition, candidate display, commit, English input,
and return to Chinese under the installed-service update load. It does not
claim that CI or a standalone executable proves desktop input.

## Resource and coexistence snapshot

| Process | Before | After | Result |
|---|---|---|---|
| Context Service | PID `16468`, private `1,241,088`, handles `79` | same PID/start, private `13,484,032`, handles `80` | responding, no restart |
| ContextIME Server | PID `15076`, private `58,159,104`, handles `405` | same PID/start, private `59,699,200`, handles `407` | responding, no restart |
| official Weasel Server | PID `1528`, private `53,800,960`, handles `336` | same PID/start, private `53,800,960`, handles `336` | responding, coexistence retained |

The Context Service is intentionally bounded to the most recently observed
project snapshot, so the maximum dictionary remains resident during the run.
After deactivation, exact test-data removal, and a supported service restart,
its private memory returned to `1,187,840` bytes with `80` handles.

## Cleanup

The harness returned `{"mode":"cleanup","status":"OK"}`. The exact test
file `11223344556677889900aabbccddeeff.dict` no longer exists. One unrelated
existing project dictionary remained. Context Service restarted as PID `23624`
and all three service endpoints were present:

```text
ContextIME.ContextService.v1
ContextIME.ProjectIndexer.v1
ContextIME.ProjectCandidate.v1
```

## Raw host evidence

| File | Bytes | SHA-256 |
|---|---:|---|
| `notepad-under-installed-pressure.json` | 3,853 | `3d08b192d633f9cbe988c3bc52d0e27a63a97a09e216c8fb0157b8dc9a05823d` |
| `notepad-under-installed-pressure.json.candidate.png` | 7,539 | `913c4d0cf5f8e8c539ea5680295a397b8ad5ac1874f84509436caa6b8733379d` |
| `contextime-native-evidence.json` | 11,051 | `9990632628492a4e8b3c9b70a4b15e3e3ef03ba36309c412ae184ff3ec880ba4` |
| `contextime-native-evidence.txt` | 535 | `7de61e0c7bb6c6bc5d955009e9ece396db2296b9652926d4980e0b9684c32113` |
| `project-dictionary-installed-pressure-tests.exe` | 429,056 | `f8d595a5da82a9f69c15b774088deade53e8f29e379652cd7ae9d321a960adba` |
| `project-dictionary-performance-tests.exe` | 454,656 | `2b59450d4558ebb37716a61aac6c10b25fd57849ca6f634ed342dfb25528d202` |

Evidence roots:

```text
artifacts/native-evidence/m5-project-dictionary-installed-pressure-host/20260904T011358Z/
artifacts/native-evidence/m5-project-dictionary-performance-install-0.5.1/20260904T010445Z/
artifacts/project-dictionary-host-fixtures-ci-33824760241/
```

A preceding ordinary Notepad smoke passed, but it is deliberately excluded
from acceptance because its intended pressure threads did not start due to a
PowerShell orchestration syntax error. The final run above used the compiled
installed-service harness and completed all 20 real Pipe updates.

## Verification boundary

| Capability | Status |
|---|---|
| large dictionary Store/cache/merge | `implemented / built / CI verified` |
| 100,000-entry standalone Windows fixture | `runner_runtime_verified` |
| installed Context Service + Project Indexer Pipe updates | `real_machine_verified` |
| concurrent Notepad composition/candidate/commit | `real_machine_verified` |
| runtime coexistence with official Weasel | `real_machine_verified` for this run |
| generated test-data cleanup | `real_machine_verified` |
| other Language Servers, clean machine, LAN, and RDP | `REAL_WINDOWS_VERIFICATION_REQUIRED` |

This closes only the Roadmap item “large-project indexing does not block
input.” Project Dictionary management UI and any later data sources remain
separate work.
