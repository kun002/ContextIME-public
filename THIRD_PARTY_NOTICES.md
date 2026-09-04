# Third-Party Notices

## CI124/auto-ime

- Repository: `CI124/auto-ime`
- License: MIT
- Pinned reference commit: `3873421e6ef2361f7f78324c02c378ed0e6ceca5`
- Upstream version at that commit: `0.8.1-beta`

ContextIME's Windows runtime derives its high-performance Win32 FFI and keyboard-layout switching approach from these upstream modules:

- `src/win32/ime-ffi.ts`
- `src/platforms/windows/dual-keyboard.ts`
- `src/core/state-tracker.ts`

ContextIME does not import `auto-ime` as an npm/Git dependency because the upstream repository is a complete VS Code extension rather than a stable library package, and its installation lifecycle downloads Tree-sitter WASM assets. The required Windows runtime concepts are kept in a small, attributable module instead.

ContextIME adds:

- IMM32 open-status detection for English mode inside a Chinese keyboard layout.
- `ImmGetDefaultIMEWnd` fallback for windows without a directly available input context.
- local/RDP polling profiles.
- asynchronous switch reconciliation and false-learning protection.
- injectable native ports and platform-independent tests.

The upstream MIT copyright and permission notice follows.

> MIT License
>
> Copyright (c) 2024 Auto Vim IME Contributors
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## Microsoft @vscode/tree-sitter-wasm

- Repository: `microsoft/vscode-tree-sitter-wasm`
- npm package: `@vscode/tree-sitter-wasm@0.3.1`
- License: MIT
- Copyright: Microsoft Corporation

ContextIME uses the package's prebuilt Tree-sitter core and language WASM files. The package is maintained for the grammars used by VS Code and includes pinned grammar sources in its build manifest. ContextIME does not download parser binaries during `postinstall`; npm installs the declared package contents instead.

The package includes its own license and third-party component manifest. Those files must remain included when ContextIME is packaged for distribution.
