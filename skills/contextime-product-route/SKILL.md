---
name: contextime-product-route
description: Audit and plan ContextIME work so it advances a distributable, configurable Windows developer input method rather than drifting into an editor-only input switcher. Use for architecture, roadmap, implementation planning, PR review, milestone selection, and route correction in the ContextIME repository.
---

# ContextIME Product Route

## Goal

Keep ContextIME on the shortest verifiable route to an installable Windows input method for developers.

The product must provide real input-method composition and candidates, while optional editor adapters improve context accuracy. A VS Code extension or input-state switcher is not the finished product.

## Required inputs

Read, in order:

1. `/AGENTS.md`
2. `/docs/product-intent.md`
3. `/docs/product-plan.md`
4. `/docs/technical-route.md`
5. `/docs/roadmap.md`
6. the code and upstream projects relevant to the requested milestone.

If old code, prototype documentation or conversation wording conflicts with these sources, use the product plan and technical route unless the user explicitly changes the product direction.

## Workflow

### 1. Restate the product outcome

Express the requested result as something an end user can install, observe, or verify.

Reject implementation-shaped goals such as “add another adapter” unless they connect to a user-visible product result.

### 2. Classify the work

Choose exactly one primary classification:

- native IME;
- shared core;
- optional adapter;
- prototype maintenance.

State why the classification is correct.

### 3. Audit route leverage

Check whether the proposed work:

- advances Windows IME registration, TSF frontend, composition, candidates, engine integration, settings, or distribution;
- reuses existing mature projects instead of rebuilding commodity functionality;
- creates evidence that reduces the largest technical uncertainty;
- can be tested in the environment where the claim matters.

If not, identify the higher-leverage route and switch to it before coding.

### 4. Reuse before build

For each major subsystem, search for and compare existing foundations:

- librime/Rime for language engine, dictionaries, schemas, candidates, and learning;
- Weasel or another maintained Windows TSF frontend for registration and composition integration;
- existing candidate UI and installer infrastructure;
- current ContextIME policy and syntax prototypes for reusable differentiated behavior.

Pin the chosen upstream version or commit. Record license and divergence boundaries.

### 5. Define acceptance evidence

Do not accept only compilation or unit tests for native claims.

For the first native milestone require evidence of:

- Windows input-method registration;
- activation in a real text field;
- pinyin composition;
- candidate display;
- candidate selection and commit;
- Chinese/English state behavior;
- clean install and uninstall.

Clearly label anything not tested on real Windows, RDP, or the target application.

### 6. Protect the hot path

Reject designs that put any of the following in the per-keystroke path:

- LLM calls;
- network requests;
- whole-repository scans;
- large synchronous database operations;
- fragile global UI automation.

Typing must fail open and remain usable when context automation is unavailable.

### 7. Report before implementation

Produce:

- product outcome;
- route classification;
- current largest uncertainty;
- reused upstreams;
- minimal implementation slice;
- acceptance tests;
- rollback boundary;
- deferred work.

Then implement the minimal slice that produces the evidence.

## Route-stop conditions

Stop and correct the plan when any of these are true:

- the work presents a VSIX as the main ContextIME product;
- the native composition/candidate baseline is still missing but editor features are being expanded;
- a mature IME engine is being rebuilt without comparison evidence;
- “Windows input method” is being used to describe only keyboard-layout switching;
- success is claimed from mocks or CI where real Windows interaction is required;
- optional adapter logic duplicates native policy and state.

## Review checklist

Before approving a ContextIME change, verify:

- [ ] The user-visible product outcome is explicit.
- [ ] The change has one route classification.
- [ ] Native IME priority is preserved.
- [ ] Existing projects were evaluated before custom implementation.
- [ ] Product and prototype boundaries are named correctly.
- [ ] Performance and privacy hot-path rules are preserved.
- [ ] Evidence supports every compatibility or reliability claim.
- [ ] The next highest-leverage step is recorded.
