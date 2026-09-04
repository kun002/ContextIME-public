# M5.1 Project Dictionary Foundation CI Evidence

## Scope

This evidence covers the isolated file-backed Project Dictionary core only. It does not cover Adapter symbol collection, Context Service ingestion, librime candidate injection, candidate source UI, or real-machine input behavior.

## Fixed source

- Branch: `codex/m5-project-dictionary-foundation`
- Build commit: `6c7ff4c9ddb889e6ed1377c8b90035b3f34ebcb6`
- Pull request: [#33](https://github.com/kun002/ContextIME/pull/33)
- GitHub Actions run: [33715022860](https://github.com/kun002/ContextIME/actions/runs/33715022860)

## CI result

| Runner | Compiler gate | Result |
|---|---|---|
| `windows-2022` | MSVC C++17, `/W4 /WX` | 38/38 assertions passed |
| `ubuntu-24.04` | g++ C++17, `-Wall -Wextra -Werror -pedantic` | 38/38 assertions passed |

The same source passed both jobs. The test runner compiled `project_dictionary.cpp` and executed persistence operations against a new temporary directory.

## Verified behavior

- canonical 128-bit opaque project ID validation and traversal rejection;
- strict bounded symbol fields, UTF-8 validation, and path/control-character rejection;
- separate persisted dictionaries for two projects containing the same symbol;
- deterministic `symbol + symbol_type + source` record keys;
- monotonic frequency and last-seen refresh behavior;
- persistence across a newly constructed store instance;
- project listing, disable without data loss, and idempotent whole-project deletion;
- corrupt persisted data is rejected and not overwritten by upsert;
- fixed stable names for symbol types, sources, and diagnostic status.

Existing workspace regression on the same commit:

```text
npm run check
45 tests passed
0 failed
```

## Privacy and performance boundary

The core has no network, source parser, repository scanner, TSF, or librime dependency. Its logical record contains only:

```text
symbol
symbol_type
project_id
frequency
last_seen
source
```

File I/O is explicitly background-only and must not be called from the key-event path. Invalid, oversized, or path-like input is rejected before persistence. Future collectors remain responsible for requesting symbol names only and never forwarding source text, string values, credentials, project paths, document URIs, or environment values.

## Status boundary

| Capability | Status |
|---|---|
| Project Dictionary storage core | `implemented / built / statically_verified / runner_runtime_verified` |
| Installed ContextIME runtime changed | no |
| Workspace project ID generation | not implemented |
| Language Server / editor symbol ingestion | not implemented |
| Context Service integration | not implemented |
| librime candidate integration | not implemented |
| Candidate source display | not implemented |
| Real Windows input verification | `REAL_WINDOWS_VERIFICATION_REQUIRED` after candidate integration |

M5 is not complete. The next bounded step is a local Adapter/Language Server symbol source plus a background ingestion contract; normal input must continue to fail open if that source or store is unavailable.
