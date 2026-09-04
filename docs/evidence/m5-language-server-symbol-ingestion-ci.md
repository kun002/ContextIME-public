# M5.2 Language Server Symbol Ingestion CI Evidence

## Scope

This evidence covers the bounded local path from normalized VS Code Language Server document symbols through the isolated Project Indexer protocol to a file-backed Project Dictionary. It does not cover installed VS Code behavior, project symbols in librime candidates, candidate source UI, or real-machine typing.

## Fixed source

- Branch: `codex/m5-project-symbol-ingestion`
- Implementation commit: `17b701dd2f19734832878e1e060403f311a4a396`
- Base merge: `c477541b9e098c289618cb83ec3e13995ff7ec5a`（PR #33 / M5.1）
- Pull request: [#34](https://github.com/kun002/ContextIME/pull/34)
- Project Dictionary run: [33730311588](https://github.com/kun002/ContextIME/actions/runs/33730311588)
- Context Service run: [33730311542](https://github.com/kun002/ContextIME/actions/runs/33730311542)
- General CI run: [33730311569](https://github.com/kun002/ContextIME/actions/runs/33730311569)
- Native installer run: [33730311559](https://github.com/kun002/ContextIME/actions/runs/33730311559)
- Weasel TSF bridge regression: [33730311614](https://github.com/kun002/ContextIME/actions/runs/33730311614)

## Implemented path

```text
VS Code document symbol provider
  → name / kind / children only
  → domain-separated 128-bit opaque project ID
  → fixed 4096-byte local batches
  → \\.\pipe\ContextIME.ProjectIndexer.v1
  → Context Service dedicated project worker
  → %APPDATA%\ContextIME\project-dictionaries
```

The Adapter accepts only active workspace file documents for the bounded C/C++/C#/JavaScript/TypeScript/Python language set. Collection is capped at 256 symbols per document and is triggered by activation, active-editor, save, workspace/configuration change, or an explicit refresh command—not by every key event.

Only Language Server `name`, `kind`, and nested `children` cross the source boundary. Source text, URI, workspace path, range, detail, container, string values, credentials, Tokens, and environment values are not retained or transmitted.

## Project Dictionary workflow

Both runners compiled the M5.1 Store plus the M5.2 protocol with C++17 warnings-as-errors.

| Runner | Compiler gate | Result |
|---|---|---|
| `ubuntu-24.04` | g++ `-Wall -Wextra -Werror -pedantic` | Store 38/38 + protocol 41/41 assertions |
| `windows-2022` | MSVC `/W4 /WX` | Store 38/38 + protocol 41/41 + real Windows pipe 18/18 assertions |

The Windows test performs a real local Named Pipe exchange and reads the resulting temporary dictionary. The cross-language canonical fixtures prove TypeScript request/C++ decode and C++ response/TypeScript decode compatibility, including request ID, count, frequency, project ID, symbol types, UTF-8 bytes, response and ACK.

Protocol gates cover:

- fixed `CIPD` v1 framing, 4096-byte request and 32-byte response;
- at most 64 records per request and strict zero-reserved areas;
- malformed magic/version/type/size/operation/count/enum/UTF-8 rejection;
- client batch splitting under the record and byte caps;
- one total deadline across all batches, including Pipe-instance retry;
- success, application rejection, timeout, disconnect, service unavailable, oversized/corrupt response and ACK behavior;
- server-owned Unix epoch millisecond `last_seen` and Store persistence.

## Context Service workflow

The existing dual-platform Context Service suites remained green. The Windows runner compiled the new Store/protocol/server sources into `contextime-context-service.exe`, reported `70/70` Windows assertions, and completed:

```text
project pipe + singleton + --quit + restart
```

This verifies that `--quit` wakes and joins the dedicated project worker and that the same process can restart with both endpoints. The project worker uses a separate thread and Pipe from the Context decision loop; its file I/O is not called by TSF key events.

## Adapter and VSIX workflow

General CI passed typecheck and all `57/57` workspace tests on Windows and Ubuntu. Added Adapter tests cover:

- SymbolKind mapping and unsupported-kind omission;
- Windows workspace path normalization and opaque ID stability/isolation;
- invalid UTF-8 surrogate, control/path character and 128-byte symbol rejection;
- flattening, deduplication/frequency and the 256-symbol cap;
- request/response codec and Project Indexer client failure states;
- event-driven scheduler regression without fixed timing sleeps.

The Windows package job passed its external-module/capability audit and produced:

| Item | Value |
|---|---|
| Filename | `context-ime-0.4.0-win32-x64.vsix` |
| Size | `2,123,202` bytes |
| SHA-256 | `069017bbfa681ce01cd45d49566952a377aa058f3cfc1b80e52b76778e8013e3` |
| Files | `34` |
| Bundled extension | `76.64 KB` |

This VSIX is an optional Adapter validation package, not the ContextIME native `.exe` installer.

## Native regression

The fixed Weasel TSF bridge run passed canonical fixtures, thread/failure audits, and x64/Win32 TSF builds on the same implementation commit. The native installer run rebuilds the patched frontend and packages the Context Service containing the new project worker; it is build evidence only and does not prove installed Adapter ingestion or candidate visibility.

No code in this PR changes the TSF key-event, librime composition, candidate, selection, or commit path.

## Verification status

| Capability | Status | Evidence boundary |
|---|---|---|
| Workspace ID and Language Server symbol normalization | `implemented / statically_verified` | TypeScript unit tests |
| TypeScript/C++ Project Dictionary protocol | `implemented / built / statically_verified` | dual-platform 41 assertions + canonical fixtures |
| Windows Project Indexer Pipe → Store | `implemented / built / runner_runtime_verified` | real local Pipe, temporary dictionary, 18 assertions |
| Context Service project worker lifecycle | `implemented / built / runner_runtime_verified` | project pipe + singleton + quit + restart |
| VSIX Adapter bundle | `built / package_audited` | Windows package job; no mode-switching owner added |
| TSF/librime input regression | `built / statically_verified` | canonical fixtures and x64/Win32 build only |
| Installed VS Code → `%APPDATA%` dictionary | `REAL_WINDOWS_VERIFICATION_REQUIRED` | interactive desktop not run |
| Project symbol appears in librime candidates | `not_implemented` | next M5 blocker |
| Candidate source marker/UI | `not_implemented` | later M5 work |

## Next blocker

CI proves the bounded source, wire compatibility, local Pipe persistence and lifecycle. The next highest-leverage implementation is a fail-open librime candidate bridge that selects only the active project's enabled snapshot without adding disk I/O to the key-event path. Before that can be called usable, installed Adapter ingestion and the final candidate path both require a real, unlocked Windows interactive-desktop verification.
