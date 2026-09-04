# M5.4 Project Dictionary Management Host Verification

## Conclusion

ContextIME `0.5.2-preview` completed the installed project-dictionary
management path on a real, logged-in and unlocked Windows 11 desktop:

```text
installed Start menu shortcut
→ native project dictionary manager
→ bounded CIPM requests on ContextIME.ProjectIndexer.v1
→ existing Project Indexer single owner
→ persisted dictionary and active immutable snapshot
```

The manager displayed a 100,000-entry fixed test dictionary, moved from rows
`1-100` to `101-200` and back, disabled and enabled it without reporting
`UNAVAILABLE`, and deleted it after the product's confirmation dialog. The
active candidate snapshot changed from 256 entries to zero while disabled and
returned to 256 after enabling. The unrelated real project dictionary retained
its exact SHA-256 throughout the test.

```text
M5.4_PROJECT_DICTIONARY_MANAGEMENT = VERIFIED
```

## Fixed product and build

| Item | Value |
|---|---|
| Product | `ContextIME 0.5.2-preview` |
| Private implementation commit | `c033381` |
| Public build commit | `48740cfa6158d7334ef5d8c21e1f8613f59c704a` |
| Installer CI run | `33876802484` |
| Installer | `contextime-0.5.2-preview-installer.exe` |
| Installer bytes | `12,294,547` |
| Installer SHA-256 | `c61356c4f19351041fb805d52a7463c8c4166378dc6653eca3b0391d4db2ec5c` |
| Weasel commit | `9cc96e20dc71b80876b12f689bb5863c76c2a7ed` |
| librime commit | `1c23358157934bd6e6d6981f0c0164f05393b497` |
| OS | Windows 11 Professional x64, build `26200` |

The downloaded installer size and SHA-256 matched its generated manifest.
The same-version in-place upgrade replaced the installed Context Service and
manager with the exact manifest hashes, restarted ContextIME's own processes,
and left official Weasel running from its independent install directory. The
installer did not request a reboot.

## Defect found during acceptance

The first current-head package passed paging and immediate mutation/view but
real-host acceptance found an additional lease boundary. A management request
against the 100,000-entry dictionary could take longer than the three-second
active-project lease. A later Adapter heartbeat could then refresh the expired
in-memory snapshot and briefly revive 256 pre-disable candidates.

The rejected sequence recorded:

```text
disable → expired snapshot: 0 candidates
heartbeat while disabled → stale snapshot revived: 256 candidates
```

`ActiveProjectSnapshotCache::RefreshLease` now refuses an expired snapshot.
The existing Project Indexer owner therefore reloads the persisted dictionary
before publishing a new generation. A deterministic dual-platform assertion
prevents a later heartbeat from reviving an expired generation. No disk I/O,
IPC, lock, scan, or Node work was added to the key path.

The fixed installed build recorded:

```text
enabled generation 2: 256 candidates
disabled after lease expiry: 0 candidates
heartbeat while disabled, generation 3: 0 candidates
heartbeat after enabling, generation 4: 256 candidates
```

## Manager interaction

The installed shortcut
`ContextIME输入法\【ContextIME】项目词库.lnk` resolved to the manager in the
installed `contextime-0.5.2-preview` directory. The interactive window then
reported:

```text
已启用  |  100000 个词条  |  1-100
已启用  |  100000 个词条  |  101-200
已启用  |  100000 个词条  |  1-100
已禁用  |  100000 个词条  |  1-100
已启用  |  100000 个词条  |  1-100
```

Both mutation responses were followed by immediate views. Neither displayed
`Context Service: UNAVAILABLE`. Disable and enable continued to use the
existing background owner and immutable snapshot; the manager never opened a
dictionary file directly.

## Delete and data isolation

Only the fixed test project was removed:

```text
project ID: 11223344556677889900aabbccddeeff
fixed file bytes before removal: 6,792,034
fixed file SHA-256 before removal:
  173f8da6c4bdfe692378292b8cd9255e7efea8a70415dbb66437af1ea582215
```

The manager displayed its confirmation dialog. After choosing Yes, the file
no longer existed, the project was absent from the manager list, and the
active snapshot was inactive with zero candidates. The unrelated dictionary
remained:

```text
file: e37d5123336bb193cf26efc9ff1a31bc.dict
SHA-256: 5c9441309c389d311a300613c1234578634fc405955b31555e820dfda06da3cd
```

## Ordinary input and fail-open

The installed build passed the Notepad baseline after management verification:

```text
candidate detected: true
nihao → 你好
Shift English: abc
Chinese mode restored: true
final text: 你好abc
Passed: true
```

The first Notepad automation attempt stalled while creating the TSF profile
manager and timed out without producing an input result. It is retained as a
diagnostic timeout and is not counted as a pass. The immediate clean retry
completed every assertion.

Context Service was then stopped through its supported `--quit` path. The
Context, Project Indexer, and Project Candidate Pipes were all absent while a
second Notepad run produced the same successful result. ContextIME Server and
official Weasel remained running. Context Service was restarted afterward and
all three service Pipes reappeared.

## CI verification

The private PR workflows were rejected before job startup by the account's
GitHub Actions billing state; they did not execute or fail the code. The
sanitized public mirror executed the equivalent commit successfully:

| Workflow | Run | Result |
|---|---:|---|
| Project Dictionary, Windows MSVC and Ubuntu g++ | `33876636627` | success |
| Context Service | `33876636565` | success |
| IME Host Context Bridge | `33876636646` | success |
| General CI, Windows and Ubuntu | `33876636582` | success |
| Native Preview installer | `33876802484` | success |

The local WSL/g++ gate also passed Project Dictionary 43, Active Snapshot 11,
Indexer Protocol 47, Management Protocol 28, Candidate Protocol 11, and
Performance 12 assertions.

## Raw host evidence

Evidence root:

```text
artifacts/native-evidence/m5-project-dictionary-management-host/
  20260904T132057Z-public-preview-fixed/
```

| File | Bytes | SHA-256 |
|---|---:|---|
| `verification-summary.json` | 2,257 | `5c69a77caa69de30ab032ae4d2ae9d7e3717f5413429e768ff080e1737536c3b` |
| `manager-next-page.png` | 66,339 | `e10a8a29df9c3919e4f3c0d7011f849e7027e84f8b297dfcf1b6e5b0aa4ef707` |
| `manager-disabled-heartbeat-safe.png` | 134,916 | `1af648c41659f4ee3cef7c89f6636de35ee807a76ab5005797c2cb231fe3594a` |
| `manager-enabled-restored.png` | 130,794 | `770487dcfa4350240557381c3f5e49004abd3f26fe207e7f285b57e4778ed3c8` |
| `manager-after-test-dictionary-removal.png` | 37,739 | `188b6b24d667c5c8bf59107b19923cb92eb2608629943bef291eeca070f80c2e` |
| `notepad-baseline-retry.json` | 3,854 | `15d4d7007b092066d5f8dc5f4c115de7237b668a2ffb0bb6d1ca9ede55db82ae` |
| `notepad-service-stopped.json` | 3,855 | `4944d7b85dceb10295aac9aafc64059a64b00178d09ad0e4009be17761b15177` |

## Verification boundary

| Capability | Status |
|---|---|
| CIPM codec, client/server, Win32 manager and snapshot mutations | `implemented` |
| fixed ContextIME native package and installer manifest | `built / package_audited` |
| Windows MSVC, Ubuntu g++, real local Pipe and lifecycle gates | `CI verified` |
| installed Start menu manager, paging, disable and enable | `real_machine_verified` |
| expired lease plus disabled heartbeat regression | `real_machine_verified` |
| confirmed test-dictionary removal and unrelated-data isolation | `real_machine_verified` |
| Context Service unavailable ordinary pinyin | `real_machine_verified` |
| clean Windows, LAN, RDP and other Language Servers | `REAL_WINDOWS_VERIFICATION_REQUIRED` |

This closes M5.4 management for the main development host. It does not claim a
clean-machine Release Gate or expand project-dictionary data sources.
