import { WindowsNativePort } from './types';

/**
 * Derived from CI124/auto-ime src/win32/ime-ffi.ts at
 * 3873421e6ef2361f7f78324c02c378ed0e6ceca5 (MIT).
 *
 * The layout path is retained and the existing IMM32 bindings are completed
 * with default-IME-window open-status fallback. Koffi/user32/imm32 are loaded
 * lazily, so importing this package on Linux/macOS is safe.
 */
export class KoffiWindowsNativePort implements WindowsNativePort {
  private readonly GetForegroundWindow: () => unknown;
  private readonly GetWindowThreadProcessId: (hwnd: unknown, pid: number[]) => number;
  private readonly GetKeyboardLayout: (threadId: number) => unknown;
  private readonly GetKeyboardLayoutList: (count: number, buffer: any) => number;
  private readonly PostMessageW: (
    hwnd: unknown,
    message: number,
    wParam: bigint,
    lParam: bigint,
  ) => number;
  private readonly SendMessageW: (
    hwnd: unknown,
    message: number,
    wParam: bigint,
    lParam: bigint,
  ) => bigint;
  private readonly AttachThreadInput: (from: number, to: number, attach: number) => number;
  private readonly GetCurrentThreadId: () => number;
  private readonly ImmGetContext: (hwnd: unknown) => unknown;
  private readonly ImmReleaseContext: (hwnd: unknown, context: unknown) => number;
  private readonly ImmGetOpenStatus: (context: unknown) => number;
  private readonly ImmSetOpenStatus: (context: unknown, open: number) => number;
  private readonly ImmGetDefaultIMEWnd: (hwnd: unknown) => unknown;

  constructor() {
    if (process.platform !== 'win32') {
      throw new Error('KoffiWindowsNativePort is only available on Windows.');
    }

    const koffi: any = require('koffi');
    const HANDLE = koffi.pointer('HANDLE', koffi.opaque());
    const HWND = koffi.alias('HWND', HANDLE);
    koffi.alias('HKL', 'uint64');
    koffi.alias('DWORD', 'uint32_t');
    koffi.alias('UINT', 'unsigned int');
    koffi.alias('WPARAM', 'uint64');
    koffi.alias('LPARAM', 'int64');
    koffi.alias('LRESULT', 'int64');
    koffi.alias('BOOL', 'int32_t');

    const user32 = koffi.load('user32.dll');
    const imm32 = koffi.load('imm32.dll');
    const kernel32 = koffi.load('kernel32.dll');

    this.GetForegroundWindow = user32.func('HWND __stdcall GetForegroundWindow()');
    this.GetWindowThreadProcessId = user32.func(
      'DWORD __stdcall GetWindowThreadProcessId(HWND, _Out_ DWORD*)',
    );
    this.GetKeyboardLayout = user32.func('HKL __stdcall GetKeyboardLayout(DWORD)');
    this.GetKeyboardLayoutList = user32.func(
      'int __stdcall GetKeyboardLayoutList(int, void*)',
    );
    this.PostMessageW = user32.func(
      'BOOL __stdcall PostMessageW(HWND, UINT, WPARAM, LPARAM)',
    );
    this.SendMessageW = user32.func(
      'LRESULT __stdcall SendMessageW(HWND, UINT, WPARAM, LPARAM)',
    );
    this.AttachThreadInput = user32.func(
      'BOOL __stdcall AttachThreadInput(DWORD, DWORD, BOOL)',
    );
    this.GetCurrentThreadId = kernel32.func('DWORD __stdcall GetCurrentThreadId()');
    this.ImmGetContext = imm32.func('void* __stdcall ImmGetContext(HWND)');
    this.ImmReleaseContext = imm32.func(
      'BOOL __stdcall ImmReleaseContext(HWND, void*)',
    );
    this.ImmGetOpenStatus = imm32.func('BOOL __stdcall ImmGetOpenStatus(void*)');
    this.ImmSetOpenStatus = imm32.func(
      'BOOL __stdcall ImmSetOpenStatus(void*, BOOL)',
    );
    this.ImmGetDefaultIMEWnd = imm32.func(
      'HWND __stdcall ImmGetDefaultIMEWnd(HWND)',
    );
  }

  getForegroundLanguageId(): number {
    const hwnd = this.getTrueForegroundWindow();
    if (hwnd === 0n) return 0;

    const pid = [0];
    const threadId = this.GetWindowThreadProcessId(hwnd, pid);
    const layout = BigInt(this.GetKeyboardLayout(threadId) as any);
    return Number(layout & 0xffffn);
  }

  enumerateLanguageIds(): number[] {
    const count = this.GetKeyboardLayoutList(0, null);
    if (count <= 0) return [];

    const buffer = Buffer.alloc(count * 8);
    this.GetKeyboardLayoutList(count, buffer);
    const ids: number[] = [];
    for (let index = 0; index < count; index += 1) {
      ids.push(Number(buffer.readBigUInt64LE(index * 8) & 0xffffn));
    }
    return [...new Set(ids)];
  }

  requestLanguageId(languageId: number): boolean {
    const hwnd = this.getTrueForegroundWindow();
    if (hwnd === 0n) return false;
    const WM_INPUTLANGCHANGEREQUEST = 0x0050;
    return this.PostMessageW(hwnd, WM_INPUTLANGCHANGEREQUEST, 0n, BigInt(languageId)) !== 0;
  }

  getImeOpenStatus(): boolean | null {
    const hwnd = this.getTrueForegroundWindow();
    if (hwnd === 0n) return null;

    const context = this.ImmGetContext(hwnd);
    if (context) {
      try {
        return this.ImmGetOpenStatus(context) !== 0;
      } finally {
        this.ImmReleaseContext(hwnd, context);
      }
    }

    const imeWindow = this.ImmGetDefaultIMEWnd(hwnd);
    if (!imeWindow) return null;
    const WM_IME_CONTROL = 0x0283;
    const IMC_GETOPENSTATUS = 0x0005;
    return Number(this.SendMessageW(imeWindow, WM_IME_CONTROL, BigInt(IMC_GETOPENSTATUS), 0n)) !== 0;
  }

  setImeOpenStatus(open: boolean): boolean {
    const hwnd = this.getTrueForegroundWindow();
    if (hwnd === 0n) return false;

    const context = this.ImmGetContext(hwnd);
    if (context) {
      try {
        return this.ImmSetOpenStatus(context, open ? 1 : 0) !== 0;
      } finally {
        this.ImmReleaseContext(hwnd, context);
      }
    }

    const imeWindow = this.ImmGetDefaultIMEWnd(hwnd);
    if (!imeWindow) return false;
    const WM_IME_CONTROL = 0x0283;
    const IMC_SETOPENSTATUS = 0x0006;
    const result = this.SendMessageW(
      imeWindow,
      WM_IME_CONTROL,
      BigInt(IMC_SETOPENSTATUS),
      open ? 1n : 0n,
    );
    return Number(result) === 0;
  }

  private getTrueForegroundWindow(): bigint {
    let hwnd = BigInt(this.GetForegroundWindow() as any);
    if (hwnd === 0n) return 0n;

    const pid = [0];
    const targetThreadId = this.GetWindowThreadProcessId(hwnd, pid);
    const currentThreadId = Number(this.GetCurrentThreadId());
    if (currentThreadId !== targetThreadId) {
      const attached = this.AttachThreadInput(currentThreadId, targetThreadId, 1);
      if (attached) {
        try {
          hwnd = BigInt(this.GetForegroundWindow() as any);
        } finally {
          this.AttachThreadInput(currentThreadId, targetThreadId, 0);
        }
      }
    }
    return hwnd;
  }
}
