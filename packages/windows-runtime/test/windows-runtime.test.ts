import assert from 'node:assert/strict';
import test from 'node:test';
import {
  KoffiWindowsNativePort,
  RuntimeLogger,
  WindowsImeRuntime,
  WindowsNativePort,
} from '../src/index';

class FakePort implements WindowsNativePort {
  requested: number[] = [];
  openRequests: boolean[] = [];

  constructor(
    public current: number,
    public layouts: number[] = [1033, 2052],
    public openStatus: boolean | null = true,
    public accept = true,
    public applyLayoutImmediately = true,
  ) {}

  getForegroundLanguageId(): number {
    return this.current;
  }

  enumerateLanguageIds(): number[] {
    return this.layouts;
  }

  requestLanguageId(languageId: number): boolean {
    this.requested.push(languageId);
    if (this.accept && this.applyLayoutImmediately) this.current = languageId;
    return this.accept;
  }

  getImeOpenStatus(): boolean | null {
    return this.openStatus;
  }

  setImeOpenStatus(open: boolean): boolean {
    this.openRequests.push(open);
    if (this.accept) this.openStatus = open;
    return this.accept;
  }
}

const logger: RuntimeLogger = {
  debug() {},
  info() {},
  warn() {},
  error() {},
};

test('loads production Koffi bindings on Windows', { skip: process.platform !== 'win32' }, () => {
  const port = new KoffiWindowsNativePort();
  const languageIds = port.enumerateLanguageIds();
  assert.equal(Array.isArray(languageIds), true);
  assert.equal(Number.isInteger(port.getForegroundLanguageId()), true);
});

test('detects installed English and Chinese layouts', () => {
  const runtime = new WindowsImeRuntime(new FakePort(2052), logger);
  assert.equal(runtime.isReady(), true);
  assert.equal(runtime.getCurrentMode(), 'zh');
  assert.equal(runtime.getDiagnostics().strategy, 'dual-layout');
});

test('detects English mode inside a Chinese keyboard layout', () => {
  const runtime = new WindowsImeRuntime(new FakePort(2052, [1033, 2052], false), logger);
  assert.equal(runtime.getCurrentMode(), 'en');
  assert.equal(runtime.getDiagnostics().imeOpen, false);
});

test('delegates automatic English switches to the English layout', () => {
  const port = new FakePort(2052);
  const runtime = new WindowsImeRuntime(port, logger);
  const events: string[] = [];
  runtime.onDidChangeMode((event) => events.push(`${event.origin}:${event.mode}`));

  const result = runtime.switchTo('en', 1_000);
  assert.equal(result.success, true);
  assert.equal(result.method, 'layout');
  assert.deepEqual(port.requested, [1033]);
  assert.deepEqual(events, ['auto:en']);
});

test('ignores stale old layout while an async automatic switch settles', () => {
  const port = new FakePort(2052, [1033, 2052], true, true, false);
  const runtime = new WindowsImeRuntime(port, logger, { autoSwitchSuppressMs: 750 });
  const events: string[] = [];
  runtime.onDidChangeMode((event) => events.push(`${event.origin}:${event.mode}`));

  runtime.switchTo('en', 1_000);
  runtime.syncFromSystem(1_100);
  assert.deepEqual(events, ['auto:en']);
  assert.equal(runtime.getCurrentMode(), 'en');

  port.current = 1033;
  runtime.syncFromSystem(1_200);
  assert.deepEqual(events, ['auto:en']);
});

test('marks an automatic switch timeout as unknown instead of manual', () => {
  const port = new FakePort(2052, [1033, 2052], true, true, false);
  const runtime = new WindowsImeRuntime(port, logger, { autoSwitchSuppressMs: 100 });
  const events: string[] = [];
  runtime.onDidChangeMode((event) => events.push(`${event.origin}:${event.mode}`));

  runtime.switchTo('en', 1_000);
  runtime.syncFromSystem(1_101);
  assert.deepEqual(events, ['auto:en', 'unknown:zh']);
});

test('opens the IME after the Chinese layout arrives asynchronously', () => {
  const port = new FakePort(1033, [1033, 2052], false, true, false);
  const runtime = new WindowsImeRuntime(port, logger);
  runtime.switchTo('zh', 1_000);
  assert.deepEqual(port.requested, [2052]);

  port.current = 2052;
  runtime.syncFromSystem(1_100);
  assert.deepEqual(port.openRequests, [true]);
  assert.equal(runtime.getCurrentMode(), 'zh');
});

test('emits manual event for Shift toggling inside Chinese layout', () => {
  const port = new FakePort(2052, [1033, 2052], true);
  const runtime = new WindowsImeRuntime(port, logger);
  const events: string[] = [];
  runtime.onDidChangeMode((event) => events.push(`${event.origin}:${event.mode}`));

  port.openStatus = false;
  runtime.syncFromSystem(5_000);
  assert.deepEqual(events, ['manual:en']);
});

test('supports a single Chinese layout through IME open status', () => {
  const port = new FakePort(2052, [2052], true);
  const runtime = new WindowsImeRuntime(port, logger);
  assert.equal(runtime.isReady(), true);
  assert.equal(runtime.getDiagnostics().strategy, 'single-layout');

  const result = runtime.switchTo('en');
  assert.equal(result.method, 'ime-open-status');
  assert.deepEqual(port.openRequests, [false]);
});

test('reports unavailable when no Chinese layout exists', () => {
  const runtime = new WindowsImeRuntime(new FakePort(1033, [1033], null), logger);
  assert.equal(runtime.isReady(), false);
  assert.equal(runtime.switchTo('zh').method, 'unavailable');
});

test('reports a rejected foreground input request', () => {
  const runtime = new WindowsImeRuntime(new FakePort(2052, [1033, 2052], true, false), logger);
  const result = runtime.switchTo('en');
  assert.equal(result.success, false);
  assert.equal(result.method, 'failed');
  assert.equal(runtime.getCurrentMode(), 'zh');
});
