import { InputMode, ModeDecision } from './types';

export interface SwitchGuardConfig {
  switchCooldownMs: number;
  focusSettleMs: number;
  manualOverrideMs: number;
  remoteFocusSettleMs: number;
}

export const DEFAULT_SWITCH_GUARD_CONFIG: SwitchGuardConfig = {
  switchCooldownMs: 500,
  focusSettleMs: 80,
  manualOverrideMs: 5_000,
  remoteFocusSettleMs: 180,
};

/**
 * Prevents repeated, oscillating or focus-race switches. This class has no
 * platform dependencies and can be reused inside the auto-ime controller.
 */
export class SwitchGuard {
  private lastAppliedMode: InputMode | null = null;
  private lastAutoSwitchAt = Number.NEGATIVE_INFINITY;
  private focusSettledAt = Number.NEGATIVE_INFINITY;
  private manualOverrideUntil = Number.NEGATIVE_INFINITY;

  constructor(
    private readonly config: SwitchGuardConfig = DEFAULT_SWITCH_GUARD_CONFIG,
  ) {}

  notifyFocusChanged(now = Date.now(), remoteSession = false): void {
    this.focusSettledAt =
      now + (remoteSession ? this.config.remoteFocusSettleMs : this.config.focusSettleMs);
  }

  notifyManualSwitch(now = Date.now()): number {
    this.manualOverrideUntil = now + this.config.manualOverrideMs;
    return this.manualOverrideUntil;
  }

  canApply(decision: ModeDecision, now = Date.now()): boolean {
    if (!decision.shouldSwitch) return false;
    if (now < this.focusSettledAt) return false;
    if (now < this.manualOverrideUntil) return false;
    if (this.lastAppliedMode === decision.targetMode) return false;
    if (now - this.lastAutoSwitchAt < this.config.switchCooldownMs) return false;
    return true;
  }

  markApplied(mode: InputMode, now = Date.now()): void {
    this.lastAppliedMode = mode;
    this.lastAutoSwitchAt = now;
  }

  getManualOverrideUntil(): number | undefined {
    return Number.isFinite(this.manualOverrideUntil)
      ? this.manualOverrideUntil
      : undefined;
  }
}
