import {
  HabitLookup,
  InputContext,
  InputMode,
  ModeDecision,
  PolicyConfig,
  SurfaceMemoryLookup,
} from './types';

const EMPTY_SURFACE_MEMORY: SurfaceMemoryLookup = {
  lookup: () => null,
};

export const DEFAULT_POLICY_CONFIG: PolicyConfig = {
  appDefaults: {
    'codex.exe': 'zh',
    codex: 'zh',
    'windowsterminal.exe': 'en',
    'cmd.exe': 'en',
    'powershell.exe': 'en',
    pwsh: 'en',
  },
  surfaceDefaults: {
    terminal: 'en',
    search: 'en',
    commandPalette: 'en',
    addressBar: 'en',
    chat: 'zh',
    commit: 'zh',
    webText: 'zh',
  },
  habitMinSamples: 3,
  habitConfidenceThreshold: 0.75,
};

export class PolicyEngine {
  constructor(
    private readonly habits: HabitLookup,
    private readonly surfaceMemory: SurfaceMemoryLookup = EMPTY_SURFACE_MEMORY,
    private readonly config: PolicyConfig = DEFAULT_POLICY_CONFIG,
  ) {}

  decide(context: InputContext, now = Date.now()): ModeDecision {
    if (context.automationPaused) {
      return this.result(context, context.currentMode, 'paused', '自动切换已暂停', 1);
    }

    if (context.lockedMode) {
      return this.result(context, context.lockedMode, 'lock', '用户已锁定输入模式', 1);
    }

    if (context.compositionActive) {
      return this.result(
        context,
        context.currentMode,
        'composition',
        '拼音组合输入进行中，禁止自动切换',
        1,
      );
    }

    if (context.manualOverrideUntil && now < context.manualOverrideUntil) {
      return this.result(
        context,
        context.currentMode,
        'manual',
        '用户刚刚手动切换，暂时停止自动干预',
        1,
      );
    }

    const habit = this.habits.lookup(context, now);
    if (
      habit &&
      habit.samples >= this.config.habitMinSamples &&
      habit.confidence >= this.config.habitConfidenceThreshold
    ) {
      return this.result(
        context,
        habit.mode,
        'habit',
        `个人习惯命中：${habit.samples} 次样本`,
        habit.confidence,
      );
    }

    const syntaxMode = this.modeForSyntax(context);
    if (syntaxMode) {
      return this.result(
        context,
        syntaxMode,
        'syntax',
        `语法上下文：${context.syntax}`,
        0.98,
      );
    }

    const rememberedMode = this.surfaceMemory.lookup(context, now);
    if (rememberedMode) {
      return this.result(
        context,
        rememberedMode,
        'memory',
        `恢复输入区域记忆：${context.controlId ?? context.surface}`,
        0.96,
      );
    }

    const surfaceMode = this.config.surfaceDefaults[context.surface];
    if (surfaceMode) {
      return this.result(
        context,
        surfaceMode,
        'surface',
        `输入区域规则：${context.surface}`,
        0.94,
      );
    }

    const appMode = this.config.appDefaults[context.appId.toLowerCase()];
    if (appMode) {
      return this.result(
        context,
        appMode,
        'application',
        `应用默认规则：${context.appId}`,
        0.9,
      );
    }

    return this.result(
      context,
      context.currentMode,
      'fallback',
      '没有高置信度规则，保持当前模式',
      0.5,
    );
  }

  private modeForSyntax(context: InputContext): InputMode | null {
    switch (context.syntax) {
      case 'code':
      case 'markdownCode':
        return 'en';
      case 'comment':
      case 'markdownText':
        return 'zh';
      case 'string':
      case 'unknown':
        return null;
    }
  }

  private result(
    context: InputContext,
    targetMode: InputMode,
    source: ModeDecision['source'],
    reason: string,
    confidence: number,
  ): ModeDecision {
    return {
      targetMode,
      shouldSwitch: targetMode !== context.currentMode,
      source,
      reason,
      confidence,
    };
  }
}
