import assert from 'node:assert/strict';
import test from 'node:test';
import {
  HabitLearner,
  InputContext,
  PolicyEngine,
  SurfaceModeMemory,
  SwitchGuard,
} from '../src/index';

function context(patch: Partial<InputContext> = {}): InputContext {
  return {
    appId: 'Code.exe',
    surface: 'editor',
    syntax: 'code',
    currentMode: 'zh',
    languageId: 'csharp',
    controlId: 'file:///project/Test.cs',
    ...patch,
  };
}

test('code switches to English', () => {
  const engine = new PolicyEngine(new HabitLearner());
  const result = engine.decide(context());
  assert.equal(result.targetMode, 'en');
  assert.equal(result.source, 'syntax');
  assert.equal(result.shouldSwitch, true);
});

test('comment switches to Chinese', () => {
  const engine = new PolicyEngine(new HabitLearner());
  const result = engine.decide(context({ syntax: 'comment', currentMode: 'en' }));
  assert.equal(result.targetMode, 'zh');
  assert.equal(result.source, 'syntax');
});

test('terminal rule has priority over application fallback', () => {
  const engine = new PolicyEngine(new HabitLearner());
  const result = engine.decide(
    context({ appId: 'Code.exe', surface: 'terminal', syntax: 'unknown' }),
  );
  assert.equal(result.targetMode, 'en');
  assert.equal(result.source, 'surface');
});

test('composition keeps current mode', () => {
  const engine = new PolicyEngine(new HabitLearner());
  const result = engine.decide(
    context({ syntax: 'code', currentMode: 'zh', compositionActive: true }),
  );
  assert.equal(result.targetMode, 'zh');
  assert.equal(result.source, 'composition');
  assert.equal(result.shouldSwitch, false);
});

test('paused automation keeps current mode before all automatic rules', () => {
  const engine = new PolicyEngine(new HabitLearner());
  const result = engine.decide(context({ automationPaused: true }));
  assert.equal(result.targetMode, 'zh');
  assert.equal(result.source, 'paused');
  assert.equal(result.shouldSwitch, false);
});

test('manual lock has priority over learned and syntax rules', () => {
  const engine = new PolicyEngine(new HabitLearner());
  const result = engine.decide(context({ lockedMode: 'zh' }));
  assert.equal(result.targetMode, 'zh');
  assert.equal(result.source, 'lock');
});

test('habit overrides syntax after enough explicit corrections', () => {
  const habits = new HabitLearner();
  const ctx = context({ syntax: 'string', currentMode: 'zh' });
  for (let i = 0; i < 5; i += 1) {
    habits.recordCorrection(ctx, 'zh', 'en');
  }

  const engine = new PolicyEngine(habits);
  const result = engine.decide(ctx);
  assert.equal(result.targetMode, 'en');
  assert.equal(result.source, 'habit');
});

test('habit snapshots survive export and replace', () => {
  const source = new HabitLearner();
  const ctx = context({ syntax: 'string' });
  source.recordCorrection(ctx, 'zh', 'en', 1_000);
  source.recordCorrection(ctx, 'zh', 'en', 1_001);

  const restored = new HabitLearner();
  restored.replaceSnapshots(source.exportSnapshots(), 1_002);
  const preference = restored.lookup(ctx, 1_002);

  assert.equal(preference?.mode, 'en');
  assert.equal(preference?.samples, 2);
});

test('surface memory restores explicit mode only when syntax has no stronger rule', () => {
  const memory = new SurfaceModeMemory();
  const ctx = context({ syntax: 'unknown', currentMode: 'en', surface: 'chat' });
  memory.remember(ctx, 'zh', 1_000);

  const engine = new PolicyEngine(new HabitLearner(), memory);
  const remembered = engine.decide(ctx, 1_001);
  assert.equal(remembered.targetMode, 'zh');
  assert.equal(remembered.source, 'memory');

  const code = engine.decide({ ...ctx, syntax: 'code', currentMode: 'zh' }, 1_001);
  assert.equal(code.targetMode, 'en');
  assert.equal(code.source, 'syntax');
});

test('switch guard prevents focus races and duplicate switches', () => {
  const guard = new SwitchGuard({
    switchCooldownMs: 500,
    focusSettleMs: 80,
    manualOverrideMs: 5_000,
    remoteFocusSettleMs: 180,
  });
  const decision = {
    targetMode: 'en' as const,
    shouldSwitch: true,
    source: 'syntax' as const,
    reason: 'code',
    confidence: 0.98,
  };

  guard.notifyFocusChanged(1_000, true);
  assert.equal(guard.canApply(decision, 1_100), false);
  assert.equal(guard.canApply(decision, 1_181), true);
  guard.markApplied('en', 1_181);
  assert.equal(guard.canApply(decision, 2_000), false);
});
