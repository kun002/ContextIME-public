# M5.3 Project Candidate Bridge CI Evidence

## Scope

This evidence covers the bounded active-project snapshot and fail-open candidate bridge from the M5.2 Project Dictionary into ContextIME/librime. It adds no symbol source beyond VS Code Language Server `name/kind/children`.

```text
persisted active-project dictionary
  → Context Service owner-thread immutable snapshot
  → \\.\pipe\ContextIME.ProjectCandidate.v1
  → Weasel background worker immutable snapshot
  → librime session property
  → Lua prefix candidate with 〔项目·类型〕 label
```

CI does not by itself prove installed VS Code, a visible Windows candidate window, or text commit. Those are recorded separately in [`m5-project-candidate-bridge-host-verification.md`](m5-project-candidate-bridge-host-verification.md).

## Fixed source and runs

- Branch: `codex/m5-project-candidate-bridge`
- Implementation/build commit: `ecc128cd7e1a96ee8afa9181ddaa4b9a9aadd533`
- Feature commit: `62aad0d3d47803b6a5cfdec3443564f33c1cda60`
- Build wiring fixes: `5214399e6d8376dc4f900c2ae7daffe70f866bba` and `ecc128cd7e1a96ee8afa9181ddaa4b9a9aadd533`
- Pull request: [#35](https://github.com/kun002/ContextIME/pull/35)
- Native installer: [33745116216](https://github.com/kun002/ContextIME/actions/runs/33745116216)
- Fixed Weasel build: [33745116220](https://github.com/kun002/ContextIME/actions/runs/33745116220)
- General CI: [33745116230](https://github.com/kun002/ContextIME/actions/runs/33745116230)
- Identity patch: [33745116224](https://github.com/kun002/ContextIME/actions/runs/33745116224)
- Clean evidence audit: [33745116255](https://github.com/kun002/ContextIME/actions/runs/33745116255)
- Context Service: [33745116245](https://github.com/kun002/ContextIME/actions/runs/33745116245)
- Project Dictionary: [33745116262](https://github.com/kun002/ContextIME/actions/runs/33745116262)
- IME Host: [33745116281](https://github.com/kun002/ContextIME/actions/runs/33745116281)

All listed runs completed successfully on the fixed implementation commit.

## Implemented boundary

The Project Indexer owner thread exclusively loads/ranks/bounds the active dictionary and publishes an immutable snapshot. It accepts only `language_server` records, deduplicates full symbols, orders by frequency and recency, and caps each publication at 256 candidates and 24 KiB of UTF-8 symbol bytes.

The Adapter explicitly activates/deactivates the current project and refreshes a three-second lease only while the VS Code window and supported workspace document remain active. A switch, disable, load failure, explicit deactivate, or lease expiry publishes an empty snapshot rather than retaining another project's candidates.

The Candidate Pipe response is fixed at 32 KiB and local-only. A Weasel Server worker fetches it every 500 ms and publishes a process-local immutable encoded property. `ProcessKeyEvent` only atomic-loads that memory snapshot; it calls `set_property` only for a new revision and only when no composition is active, then continues the original librime key path. Lua only parses the session property and yields at most 16 case-insensitive ASCII-prefix matches.

No key event performs file I/O, Named Pipe I/O, project scanning, Language Server work, Node.js work, PowerShell, Git, network access, or a synchronous wait on Context Service. Storage, transport, property, and Lua failures produce no project candidates and leave ordinary librime translators active.

## CI gates

| Workflow | Fixed result |
|---|---|
| Project Dictionary, Windows/Ubuntu | Store 38, active snapshot 10, Indexer protocol 47, Candidate protocol 11 assertions on each platform; Windows Pipe suite 27 assertions |
| Context Service | Windows service/lifecycle suite 70 assertions; dual-platform protocol/cache/application/editor regressions green |
| IME Host | Candidate refresh worker 8 assertions; existing refresh worker 31 and state applier 11 assertions green |
| Fixed Weasel bridge | Patch application plus Release x64 and Win32 builds completed with 0 errors |
| General CI | TypeScript check passed 59/59 tests on Windows and Ubuntu; Windows VSIX capability/package audit passed with 37 files |
| Native installer | Context Service/IME fixtures rerun, patched frontend built, required bridge data staged, manifest audited, installer generated |
| Identity and clean evidence | ContextIME isolation/patch stack and clean-runner evidence audits passed |

The first installer attempt exposed two build-only integration faults: the four new bridge translation units inherited Weasel PCH, and a public Weasel include path could not resolve the candidate protocol header. The two small build-wiring commits above disabled PCH for those files and exported the shared headers into the fixed Weasel include tree. No runtime fallback or test-specific product behavior was added.

## Built artifacts

| Item | Value |
|---|---|
| Native installer | `contextime-0.5.0-preview-installer.exe` |
| Installer bytes | `12,235,732` |
| Installer SHA-256 | `1f742aad9c169e6a2d51ecac642f5dcc3c1ac454d963c25c9a00887748e0aa07` |
| Build commit | `ecc128cd7e1a96ee8afa9181ddaa4b9a9aadd533` |
| Adapter VSIX | `context-ime-0.5.0-win32-x64.vsix` |
| VSIX bytes | `2,135,640` |
| VSIX SHA-256 | `1de1a1b76269bba71cca77c018bb1eea8ba6785c0df5f4cfada6ec917e42a225` |

The `.exe` is the ContextIME product installer. The VSIX is only the optional report-only Adapter used to supply Language Server symbols.

## Verification status

| Capability | Status | Evidence boundary |
|---|---|---|
| Active-project lease, bounded immutable snapshot and source filter | `implemented / statically_verified` | dual-platform fixtures |
| Candidate Pipe and Context Service owner lifecycle | `implemented / built / runner_runtime_verified` | Windows local Pipe and service tests |
| Weasel background fetch and immutable process snapshot | `implemented / built / statically_verified` | worker fixtures and x64/Win32 builds |
| Composition-safe librime property and Lua project translator | `implemented / built / statically_verified` | canonical/static gates and packaged data audit |
| Optional VS Code Adapter bundle | `built / package_audited` | 59 tests and 37-file VSIX audit |
| ContextIME `0.5.0-preview` installer | `built / package_audited` | fixed native installer workflow |
| Installed VS Code → visible candidate → commit | `CI cannot prove` | real-machine evidence document |
| Bridge failure → ordinary pinyin fail-open | `CI cannot prove visible runtime behavior` | real-machine fault injection document |

## Remaining boundary

This CI evidence does not validate large-project performance, other real Language Servers, project-dictionary management UI, clean-machine behavior, LAN, or RDP. It also does not authorize adding new data sources in M5.3.
