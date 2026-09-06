# M5.5 User-Approved Terms CI Evidence

## Scope

This evidence covers the user-approved manual term entry: `CIPM`
`UPSERT_TERM / REMOVE_ENTRY`, `ProjectDictionaryStore::RemoveEntry`, the
active-project snapshot publisher extension to `manual` source, and the
native manager term input / single-entry removal UI.

It does not add an ingestion source, does not change the Adapter `CIPD`
path, and does not move Store, Pipe, or UI work into the TSF/librime key
path. Installed-manager acceptance on a real desktop is tracked separately
in the roadmap M5.5 boundary and requires the next preview package.

## CI

The private repository's GitHub Actions are suspended by an account
billing failure (jobs stop before any step since 2026-09-04). Compilation
and tests therefore run on the public mirror `ContextIME-public`, which is
where the `0.5.3-preview` installers were also produced.

- Source branch: `codex/m5-approved-terms`
- Project Dictionary run: [34006681378](https://github.com/kun002/ContextIME-public/actions/runs/34006681378)
- Context Service runs: [34006681379](https://github.com/kun002/ContextIME-public/actions/runs/34006681379), [34006678945](https://github.com/kun002/ContextIME-public/actions/runs/34006678945)
- IME Host runs: [34006681371](https://github.com/kun002/ContextIME-public/actions/runs/34006681371), [34006678872](https://github.com/kun002/ContextIME-public/actions/runs/34006678872)
- General CI run: [34006681391](https://github.com/kun002/ContextIME-public/actions/runs/34006681391)

Both Project Dictionary jobs compile as warnings-as-errors. Windows uses
MSVC C++17 `/W4 /WX`; Ubuntu uses g++ C++17
`-Wall -Wextra -Werror -pedantic`.

| Gate | Windows 2022 | Ubuntu 24.04 |
|---|---:|---:|
| Project Dictionary | 53/53 | 53/53 |
| Active Project Snapshot | 12/12 | 12/12 |
| Project Indexer Protocol | 47/47 | 47/47 |
| Project Management Protocol | 46/46 | 46/46 |
| Project Candidate Protocol | 11/11 | 11/11 |
| Project Dictionary Performance | 12/12 | 12/12 |
| Project Indexer Windows Pipe | 59/59 | not applicable |

Fixed coverage added for M5.5:

- `UPSERT_TERM / REMOVE_ENTRY` request round-trips with the bounded entry
  layout at offset 40 (`u16 length`, type, source, UTF-8 symbol, zero
  padding), UTF-8 terms, 128-byte acceptance, 129-byte rejection;
- `UPSERT_TERM` locked to `term / manual`, rejects zero project IDs,
  empty symbols, and path separators; legacy operations still reject any
  nonzero request data area;
- responses carry only resulting metadata and reject entry payloads;
- Store `RemoveEntry` removes exactly one `symbol + symbol_type + source`
  key, persists across reload, is idempotent for absent keys and missing
  projects, never creates a file, and rejects invalid keys;
- real Windows pipe: manual term added through `UPSERT_TERM` persists with
  server-owned `last_seen`, reaches the active candidate snapshot, stays a
  monotonic refresh on re-add, is rejected with `NOT_FOUND` for an
  unlisted project, leaves the active snapshot after `REMOVE_ENTRY`, and a
  disabled project keeps an empty candidate snapshot;
- snapshot publisher bridges the manual source with unchanged
  frequency/recency ordering, dedup, and 256-entry / 24 KiB limits.

## Boundary

- CI compiles and tests the management/term path only; it does not replace
  installed-manager desktop acceptance.
- The 0.5.3-preview installer produced for this branch contains the M5.5
  code but keeps the 0.5.3 identity; the installed acceptance package
  should follow the next identity bump.
- Chinese terms are stored and displayed but do not prefix-match pinyin
  input until a pinyin annotation exists.
