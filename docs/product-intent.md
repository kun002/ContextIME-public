# ContextIME Product Intent

## Problem

Software developers frequently alternate between Chinese explanation and English technical content while coding. Existing input methods provide composition and candidates but do not understand development context. Existing automatic switchers can change modes but are not complete, distributable developer input methods.

## Product outcome

ContextIME is a Windows input method that other developers can install and configure for their own workflow.

A successful product is not measured by whether one VS Code prototype can switch keyboard layouts. It is measured by whether another developer can install ContextIME, type normally in multiple applications, configure development-specific behavior, and keep using it as their daily input method.

## Target user

A developer who:

- writes Chinese comments, documentation, commit messages, and AI-agent prompts;
- writes English identifiers, paths, commands, URLs, code, and technical terms;
- wants fewer explicit Chinese/English mode switches;
- expects normal input-method reliability and candidate quality;
- needs local configuration and privacy.

## Core jobs

1. Provide reliable Chinese phonetic input, candidate selection, learning, and commit.
2. Provide fast English input without mode confusion.
3. Adapt behavior to application, input surface, syntax context, and personal habits.
4. Allow project-specific vocabulary without uploading source code.
5. Install, upgrade, remove, and migrate configuration like a normal Windows product.

## Product layers

### Native IME layer

Required product foundation:

- Windows input-method registration and activation;
- TSF/text-service integration;
- composition lifecycle;
- candidate window and selection;
- Chinese/English mode state;
- installer, updater, and uninstaller.

### Reused language engine

Prefer a mature engine such as librime for:

- pinyin segmentation;
- dictionaries and schemas;
- candidate generation and ranking;
- user learning;
- simplified/traditional configuration.

The project should customize and extend mature components rather than recreate commodity IME behavior.

### Developer-context layer

Differentiating product capability:

- code defaults to English;
- comments and documentation can default to Chinese;
- strings are configurable by language/project;
- paths, URLs, commands, filenames, and CamelCase can temporarily use English;
- project dictionaries learn approved identifiers and technical vocabulary;
- manual actions always override automation.

### Optional adapters

Editor adapters may provide precise syntax and focus information, but the IME must remain useful without them.

Adapters must communicate with the native IME through a small, versioned interface and must not duplicate the full policy or input engine.

## Current repository interpretation

The existing VSIX and TypeScript packages are a controller prototype and research assets. They validated several technical ideas:

- context policy ordering;
- Windows mode-state observation;
- syntax classification;
- local learning and stability guards;
- packaging and CI discipline.

They did not validate the central product milestone: a native Windows input method with composition and candidates.

## Route decision rules

Before starting work, choose one classification:

- **Native IME:** directly advances installation, TSF frontend, composition, candidates, engine integration, settings, or distribution.
- **Shared core:** reusable logic needed by the native IME and optional adapters.
- **Optional adapter:** improves a supported editor but is not required for basic typing.
- **Prototype maintenance:** fixes or documents the old controller prototype without presenting it as the product.

Native IME work has priority until the first native milestone passes.

## First native milestone acceptance

Evidence must show:

- ContextIME listed in Windows input methods;
- activation in a standard desktop text control;
- pinyin composition visible;
- candidate list visible;
- candidate selection commits Chinese text;
- Chinese and English state changes work;
- no typing freeze during ordinary use;
- clean install and uninstall instructions verified.

## Non-goals for the first milestone

- universal editor context detection;
- AI prediction;
- cloud synchronization;
- full project indexing;
- mobile support;
- replacing every feature of commercial IMEs;
- advanced visual customization.

## Decision test

For every proposed feature, answer:

> Does this make ContextIME a better installable developer input method, or does it merely make the current prototype look more complete?

Prefer the former. Reject or defer the latter unless explicitly requested.
