# ContextIME Agent Instructions

## Product identity

ContextIME is a distributable, configurable Windows input method for software developers.

It must eventually:

- install into the Windows input-method list;
- provide normal Chinese phonetic composition, candidates, selection, and commit;
- work across applications without requiring an editor extension;
- let each developer configure code, comment, string, terminal, chat, and project vocabulary behavior;
- use editor adapters only as optional high-precision context providers.

ContextIME is **not** primarily a VS Code extension, an input-mode switcher, or a wrapper that only controls another IME.

## Required source of truth

Before planning or implementing non-trivial work, read:

1. [`docs/product-intent.md`](docs/product-intent.md) — product identity and route decision;
2. [`docs/product-plan.md`](docs/product-plan.md) — product scope, milestones and acceptance;
3. [`docs/technical-route.md`](docs/technical-route.md) — architecture, upstream reuse, module boundaries and implementation order.

If repository code, old roadmap entries or prototype behavior conflict with these documents, the product plan and technical route take priority until the user explicitly changes them.

## Hard route constraints

1. The next product milestone must advance the installable Windows IME path.
2. Do not add editor-only features while the native IME baseline is missing, unless the task explicitly requests prototype maintenance.
3. Reuse mature IME foundations such as librime and an existing Windows TSF/Weasel implementation where technically and legally suitable.
4. Do not rebuild pinyin segmentation, dictionaries, candidate ranking, or candidate UI from zero without a written comparison proving reuse is unsuitable.
5. Do not call a VSIX, input-state controller, or keyboard-layout switcher the finished ContextIME product.
6. Do not claim TSF composition, RDP stability, installation, or application compatibility without executable evidence from the relevant environment.
7. Preserve working prototype code, but clearly label prototype components and prevent them from defining the product architecture.
8. M1 native input-method acceptance has priority over Chat, Search, Command Palette, Shader or other editor-specific refinements.

## Required planning output

Before a non-trivial change, state:

- the user-visible product outcome;
- whether the change advances the native IME, shared context engine, optional adapter, or prototype only;
- existing projects/libraries being reused;
- the shortest route to a verifiable result;
- acceptance tests and rollback boundary.

If the proposed work does not advance the requested outcome, stop and correct the route before coding.

## Architecture direction

```text
ContextIME Windows IME
├─ reusable IME engine (prefer librime)
├─ Windows TSF/input-method frontend
├─ candidate and settings UI
├─ developer-context policy
├─ local user/project dictionaries
└─ optional adapters
   ├─ VS Code
   ├─ Visual Studio
   └─ JetBrains
```

Current packages under `packages/` are prototype and reusable research assets:

- `policy-core`: candidate shared policy logic; retain only rules that make sense inside the real IME.
- `syntax-runtime`: reusable syntax-context engine.
- `vscode-adapter`: optional adapter/prototype, not the product entry point.
- `windows-runtime`: controller prototype; do not mistake keyboard-layout switching for a native IME frontend.

## First native milestone

The first acceptable native milestone is:

1. ContextIME installs and appears in the Windows input-method selector.
2. A user can activate it in a normal desktop text field.
3. Typing pinyin starts composition.
4. A candidate list appears.
5. Number keys or selection commit Chinese text.
6. English mode and Chinese mode both work.
7. Installation and removal are repeatable on a clean Windows test environment.

Until this milestone is demonstrated, prioritize native bootstrap, upstream reuse, packaging, and smoke tests over advanced context automation.

## Performance and privacy

- No LLM, cloud request, or repository scan in the per-keystroke hot path.
- Composition and candidate interaction must remain responsive under RDP.
- Project vocabulary extraction must be local, explicit, bounded, and must not upload source code.
- Store only the minimum data needed for user learning and configuration.
- Fail open: when context detection fails, preserve normal typing rather than blocking input.

## Verification

Run existing checks for affected prototype packages:

```bash
npm install --ignore-scripts
npm run check
```

Native IME work must additionally include Windows-specific build, registration, activation, composition, candidate, commit, uninstall, and clean-machine evidence. Unit tests alone are insufficient.

## Pull requests

PR descriptions must include:

- Product outcome
- Route classification: native IME / shared core / optional adapter / prototype maintenance
- Reused upstream and pinned version or commit
- Evidence produced
- Known unverified boundaries
- Next highest-leverage step

Do not merge a route-changing PR whose product outcome is ambiguous.
