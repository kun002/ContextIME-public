# M5 Project Dictionary Performance Evidence

## Scope

This evidence covers bounded background updates for a maximum-size Project
Dictionary. It does not add a symbol source and does not move Store, Pipe,
Language Server, scan, Node, or filesystem work into the TSF/librime key path.

The single-owner `ProjectDictionaryStore` retains only its most recently
observed immutable snapshot. A Project Indexer request still contains at most
64 records; the Store normalizes that bounded batch and linearly merges it with
the sorted persisted snapshot. After a successful atomic save, the exact
persisted snapshot is returned to Project Indexer Server for active-project
publication, avoiding a second full parse of the same dictionary.

The cache is validated against the dictionary file's existence, size, and last
write time. Disable/delete update or invalidate it. A fixed regression replaces
a cached file with differently sized corrupt data and proves the next upsert
returns `CORRUPT_DATA`, clears its snapshot output, and preserves the corrupt
file for diagnosis.

## CI

- Source commit: `5a73bce24c123a29419191a99aaf0da1128f6f6d`
- Project Dictionary run: [33766211854](https://github.com/kun002/ContextIME/actions/runs/33766211854)
- Context Service run: [33766211711](https://github.com/kun002/ContextIME/actions/runs/33766211711)
- IME Host run: [33766211768](https://github.com/kun002/ContextIME/actions/runs/33766211768)
- General CI run: [33766211840](https://github.com/kun002/ContextIME/actions/runs/33766211840)

Both Project Dictionary jobs compile as warnings-as-errors. Windows uses
MSVC C++17 `/W4 /WX`; Ubuntu uses g++ C++17
`-Wall -Wextra -Werror -pedantic`.

| Gate | Windows 2022 | Ubuntu 24.04 |
|---|---:|---:|
| Project Dictionary | 43/43 | 43/43 |
| Active Project Snapshot | 10/10 | 10/10 |
| Project Indexer Protocol | 47/47 | 47/47 |
| Project Candidate Protocol | 11/11 | 11/11 |
| Project Indexer Windows Pipe | 27/27 | not applicable |
| 100,000-entry performance fixture | 12/12 | 12/12 |

CI performance metrics:

| Metric | Windows 2022 | Ubuntu 24.04 |
|---|---:|---:|
| persisted bytes | 7,124,034 | 7,124,034 |
| seed | 565 ms | 270 ms |
| restart load | 997 ms | 494 ms |
| active snapshot publication | 53 ms | 68 ms |
| 4 x 64-record incremental updates | 2,592 ms | 993 ms |
| concurrent immutable snapshot reads | 11,316,168 | 3,883,675 |
| maximum measured snapshot read | 1,101 us | 1,996 us |

The incremental-update budget is 10 seconds for all four rounds. The local
pre-optimization implementation required approximately 14.3 seconds; the
optimized local g++ run required 1.724 seconds.

## Windows runner-runtime replay

The Windows artifact from run `33766211854` was downloaded and executed on the
logged-in Windows 11 Pro Build 26200 development host:

| Item | Value |
|---|---|
| file | `project-dictionary-performance-tests.exe` |
| bytes | 454,656 |
| SHA-256 | `6eaa85f30ac71a8bc217fa27ca76209d8caa706c7c87a79ab8c26902f0ef58ab` |
| assertions | 12/12 |
| seed | 820 ms |
| restart load | 1,398 ms |
| active snapshot publication | 81 ms |
| 4 x 64-record incremental updates | 2,779 ms |
| concurrent immutable snapshot reads | 12,679,607 |
| maximum measured snapshot read | 373 us |

This is `runner_runtime_verified` for the native fixture. It is not an
installed-product input result.

## Status boundary

| Layer | Status |
|---|---|
| implementation | `implemented` |
| local and CI compilation | `built` |
| dual-platform gates and Windows Pipe suite | `CI` |
| downloaded fixture on a real Windows host | `runner_runtime_verified` |
| installed Context Service update while Notepad composition/candidate/commit remains responsive | `real_machine_verified` |

ContextIME `0.5.1-preview` subsequently completed 20 real Project Indexer Pipe
updates against a 100,000-entry dictionary while Notepad composition,
candidate selection, commit, English input, and Chinese restoration passed on
the interactive Windows desktop. The installed-service evidence and cleanup
record are in
[`m5-project-dictionary-performance-host-verification.md`](m5-project-dictionary-performance-host-verification.md).
