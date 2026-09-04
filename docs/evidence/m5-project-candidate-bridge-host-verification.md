# M5.3 Project Candidate Bridge Host Verification

## Conclusion

ContextIME `0.5.0-preview` completed the required M5.3 end-to-end path on a real, logged-in and unlocked Windows desktop:

```text
VS Code TypeScript Language Server symbols
→ installed Context Service / Project Dictionary
→ active-project immutable snapshot
→ Candidate Pipe / Weasel background cache
→ ContextIME/librime candidate window
→ space commit
```

Typing `player` displayed `PlayerController 〔项目·类〕` as the first candidate and Space committed `PlayerController`. With Context Service stopped and all three service Pipes absent, Notepad ordinary pinyin still committed `你好`, English `abc` passed through, and Chinese mode restored.

```text
M5.3_PROJECT_CANDIDATE_BRIDGE = VERIFIED
```

## Fixed product and host

| Item | Value |
|---|---|
| Product | `ContextIME 0.5.0-preview` |
| Build commit | `ecc128cd7e1a96ee8afa9181ddaa4b9a9aadd533` |
| Installer CI run | `33745116216` |
| Installer | `contextime-0.5.0-preview-installer.exe` |
| Installer bytes | `12,235,732` |
| Installer SHA-256 | `1f742aad9c169e6a2d51ecac642f5dcc3c1ac454d963c25c9a00887748e0aa07` |
| Adapter | `context-ime-0.5.0-win32-x64.vsix` |
| VSIX bytes | `2,135,640` |
| VSIX SHA-256 | `1de1a1b76269bba71cca77c018bb1eea8ba6785c0df5f4cfada6ec917e42a225` |
| Host | `WINDOWS_TEST_HOST` |
| OS | Windows 11 Professional, build `26200` |
| Interactive desktop session | `1` |

The existing installation was upgraded with the fixed `0.5.0-preview` package. A read-only native installation audit reported `Mismatches: 0`; its JSON records ContextIME's independent TSF profile, install/uninstall registry, server, Pipe and user-data directory while official Weasel remains installed.

## Installed runtime and coexistence

The runtime snapshot recorded:

- ContextIME `WeaselServer.exe` from `C:\Program Files\ContextIME\contextime-0.5.0-preview`;
- `contextime-context-service.exe` from the same independent install directory;
- official Weasel `WeaselServer.exe` from `C:\Program Files\Rime\weasel-0.17.4`;
- ContextIME Native Pipe `<user-scope>\ContextIMENamedPipe`;
- service Pipes `ContextIME.ContextService.v1`, `ContextIME.ProjectIndexer.v1`, and `ContextIME.ProjectCandidate.v1`.

This confirms runtime coexistence for the exercised path; it does not repeat the full M1 install/uninstall coexistence matrix.

## Ordinary pinyin pre-check

Before the VS Code candidate test, the installed build passed the existing Notepad baseline:

```text
input: nihao
committed: 你好
candidate detected: true
English abc: true
Chinese mode restored: true
Passed: true
Errors: []
```

This established that the fixed package still had a working normal input path before project-candidate verification.

## VS Code Language Server to candidate

The smoke host created a real TypeScript workspace containing only:

```typescript
class PlayerController {
  spawnPlayer() {}
}

//
```

It installed the isolated report-only `0.5.0` VSIX and launched VS Code with workspace trust disabled so the built-in TypeScript Language Service could run in the test workspace. No ContextIME product runtime behavior was changed for the test.

The Adapter log recorded:

```text
[project:window-focus] project=e37d5123336b… language=typescript symbols=2 transport=ok accepted=2 batches=1 elapsed=5ms
```

The complete opaque project ID was `e37d5123336bb193cf26efc9ff1a31bc`. The installed dictionary contained exactly the expected Language Server records:

```text
class  PlayerController  language_server
method spawnPlayer       language_server
```

At the comment input position, typing `player` produced a visible candidate/composition change of `113,043` pixels. The saved screenshot clearly shows:

```text
1. PlayerController 〔项目·类〕
2. 普拉亚二
3. 普拉亚
...
```

Space committed `PlayerController`, the final editor suffix was `// PlayerController`, and the smoke result reported:

```text
VSCodeProjectSymbolsReady: true
ProjectDictionaryContainsSymbol: true
CandidateWindowDetected: true
ProjectCandidateCommitMatched: true
Passed: true
Errors: []
```

This is real-machine evidence for the TypeScript source exercised here. It does not generalize to all configured Language Server IDs.

## Context Service failure injection

The installed Context Service was stopped through its supported `--quit` path. The stop snapshot confirmed:

```json
{"QuitExitCode":0,"ServiceRunning":false,"RemainingServicePipes":[]}
```

Thus the Context decision, Project Indexer and Project Candidate Pipes were all absent throughout the following Notepad test. The unchanged baseline then produced:

```text
nihao → 你好
candidate detected: true
English abc: true
Chinese mode restored: true
Passed: true
Errors: []
```

The service was restarted afterward. PID `14904` owned the restored runtime and all three service Pipes reappeared. This verifies fail-open behavior for a fully unavailable bridge: project candidates disappear, while the original TSF → Weasel/librime input chain remains usable.

## Raw host evidence

Candidate evidence root:

```text
artifacts/native-evidence/m5-project-candidate-bridge-host/
```

| File | Bytes | SHA-256 |
|---|---:|---|
| `vscode-project-candidate-retry2.json` | 5,255 | `e207fdbc081e92b7ea4474c8e3248e60e503cea15ceb5bf83f889b64f6524d37` |
| `vscode-project-candidate-retry2.json.project-candidate.png` | 88,025 | `657cf8ad6d3131553a17b21094fa683a42a5e307097014766de172378cdbf74d` |
| `vscode-project-candidate-retry2.json.project-dictionary.dict` | 167 | `6451ee46042d51383e25edd56220a8ebabb7f844ffef7153cd69648332f5f3ad` |
| `vscode-adapter.log` | 1,752 | `b046aa59dab3a2c7b3e13abb9e23d6a9165bd2ce7b8640b3434019d224cfe549` |
| `notepad-baseline.json` | 3,806 | `cb45a81bc462f6c0bcf95b5615683f08a0a4c3ae474b006d18245b004b195d14` |
| `notepad-candidate-service-down.json` | 3,818 | `423175e9dcd8c9f4a3b3f65a1a94152d987041a71ff5f48bdfe6d8a93f67f29e` |
| `candidate-service-stopped.json` | 70 | `3a87d71dada456365fa67b36495d9345bc96a2cd83e867b22776aa96c2aae39b` |
| `candidate-service-restored.json` | 135 | `556a11168a202ccd7beed491aef830428e89b28b325b589e535c4464cfc90607` |
| `runtime-snapshot.json` | 1,833 | `6f8293f49ed514730a6293a7d674ad0fef875c1efb541f76e30dc4ee89ee36ec` |

Installation audit root:

```text
artifacts/native-evidence/m5-project-candidate-bridge-install/20260903T104427Z/
```

| File | Bytes | SHA-256 |
|---|---:|---|
| `contextime-native-evidence.json` | 11,094 | `e3618a5e62178992b3af20f95139bacc68ba434f5814759dd78e2eb1b7e239f5` |
| `contextime-native-evidence.txt` | 488 | `1e8ad2d64b994409ca8492cbe15105bd36744c4f7b50b2892e12f5dc7f2d8ab5` |

Two earlier candidate smoke attempts are retained as diagnostic evidence. They showed Adapter activation but no TypeScript symbols because the isolated temporary workspace had not started its Language Service. Enabling that built-in service through `--disable-workspace-trust` made the same product build pass immediately; these attempts are not product-runtime failures.

## Verification status

| Capability | Status | Evidence boundary |
|---|---|---|
| Candidate bridge implementation | `implemented` | fixed product commit |
| Native package and optional Adapter | `built / package_audited` | fixed CI runs and hashes |
| Snapshot/protocol/worker/Lua gates | `CI verified` | dual-platform and Windows runner workflows |
| Installed VS Code TypeScript symbols → dictionary | `real_machine_verified` | 2/2 symbols accepted and persisted |
| Project source marker in real candidate window | `real_machine_verified` | screenshot shows `〔项目·类〕` |
| First project candidate committed with Space | `real_machine_verified` | final suffix `// PlayerController` |
| Context Service unavailable → ordinary pinyin | `real_machine_verified` | all three service Pipes absent during Notepad pass |
| Official Weasel runtime coexistence | `runtime_verified` | independent processes, paths and Pipes |
| Other Language Servers | `REAL_WINDOWS_VERIFICATION_REQUIRED` | only built-in TypeScript exercised |
| Large-project indexing performance | `REAL_WINDOWS_VERIFICATION_REQUIRED` | bounded fixtures are not a large workspace |
| Clean machine, LAN and RDP | `REAL_WINDOWS_VERIFICATION_REQUIRED` | outside M5.3 host scope |

## Next blocker

M5.3 is closed for the requested Language Server symbol-to-candidate bridge. Remaining M5 work is project-dictionary management UI and large-project/incremental performance validation; additional data sources remain deliberately deferred.
