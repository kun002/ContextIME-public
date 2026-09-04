import { buildHabitContextKey } from './context-key';
import {
  HabitLookup,
  HabitPreference,
  InputContext,
  InputMode,
} from './types';

interface HabitCounts {
  en: number;
  zh: number;
  updatedAt: number;
}

export interface HabitSnapshot {
  key: string;
  en: number;
  zh: number;
  updatedAt: number;
}

export interface HabitLearnerOptions {
  maxEntries: number;
  maxAgeMs: number;
}

export const DEFAULT_HABIT_LEARNER_OPTIONS: HabitLearnerOptions = {
  maxEntries: 512,
  maxAgeMs: 180 * 24 * 60 * 60 * 1_000,
};

/**
 * Learns only from explicit corrections. It stores context labels and counts,
 * never the user's actual typed text.
 */
export class HabitLearner implements HabitLookup {
  private readonly counts = new Map<string, HabitCounts>();

  constructor(
    private readonly options: HabitLearnerOptions = DEFAULT_HABIT_LEARNER_OPTIONS,
  ) {}

  recordCorrection(
    context: InputContext,
    predictedMode: InputMode,
    correctedMode: InputMode,
    now = Date.now(),
  ): void {
    if (predictedMode === correctedMode) return;

    const key = buildHabitContextKey(context);
    const item = this.counts.get(key) ?? { en: 0, zh: 0, updatedAt: now };
    item[correctedMode] += 1;
    item.updatedAt = now;
    this.counts.set(key, item);
    this.prune(now);
  }

  lookup(context: InputContext, now = Date.now()): HabitPreference | null {
    const key = buildHabitContextKey(context);
    const item = this.counts.get(key);
    if (!item) return null;
    if (now - item.updatedAt > this.options.maxAgeMs) {
      this.counts.delete(key);
      return null;
    }

    const samples = item.en + item.zh;
    if (samples === 0) return null;

    const mode: InputMode = item.zh > item.en ? 'zh' : 'en';
    const dominant = Math.max(item.en, item.zh);
    const confidence = (dominant + 1) / (samples + 2);
    return { mode, confidence, samples };
  }

  exportSnapshots(): HabitSnapshot[] {
    return [...this.counts.entries()]
      .map(([key, value]) => ({ key, ...value }))
      .sort((a, b) => b.updatedAt - a.updatedAt);
  }

  replaceSnapshots(items: readonly HabitSnapshot[], now = Date.now()): void {
    this.counts.clear();
    for (const item of items) {
      if (item.en < 0 || item.zh < 0) continue;
      if (!Number.isFinite(item.updatedAt)) continue;
      this.counts.set(item.key, {
        en: item.en,
        zh: item.zh,
        updatedAt: item.updatedAt,
      });
    }
    this.prune(now);
  }

  clear(): void {
    this.counts.clear();
  }

  private prune(now: number): void {
    for (const [key, item] of this.counts) {
      if (now - item.updatedAt > this.options.maxAgeMs) this.counts.delete(key);
    }

    if (this.counts.size <= this.options.maxEntries) return;
    const keep = this.exportSnapshots().slice(0, this.options.maxEntries);
    this.counts.clear();
    for (const item of keep) {
      this.counts.set(item.key, {
        en: item.en,
        zh: item.zh,
        updatedAt: item.updatedAt,
      });
    }
  }
}
