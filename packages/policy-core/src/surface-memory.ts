import { InputContext, InputMode, SurfaceMemoryLookup } from './types';

export interface SurfaceMemorySnapshot {
  key: string;
  mode: InputMode;
  updatedAt: number;
}

export interface SurfaceMemoryOptions {
  maxEntries: number;
  maxAgeMs: number;
}

export const DEFAULT_SURFACE_MEMORY_OPTIONS: SurfaceMemoryOptions = {
  maxEntries: 256,
  maxAgeMs: 90 * 24 * 60 * 60 * 1_000,
};

/**
 * Remembers explicit user choices for a concrete input surface. It never stores
 * typed text. Syntax-aware rules still take priority over this memory.
 */
export class SurfaceModeMemory implements SurfaceMemoryLookup {
  private readonly entries = new Map<string, SurfaceMemorySnapshot>();

  constructor(
    private readonly options: SurfaceMemoryOptions = DEFAULT_SURFACE_MEMORY_OPTIONS,
  ) {}

  remember(context: InputContext, mode: InputMode, now = Date.now()): void {
    const key = buildSurfaceMemoryKey(context);
    this.entries.set(key, { key, mode, updatedAt: now });
    this.prune(now);
  }

  lookup(context: InputContext, now = Date.now()): InputMode | null {
    const item = this.entries.get(buildSurfaceMemoryKey(context));
    if (!item) return null;
    if (now - item.updatedAt > this.options.maxAgeMs) {
      this.entries.delete(item.key);
      return null;
    }
    return item.mode;
  }

  exportSnapshots(): SurfaceMemorySnapshot[] {
    return [...this.entries.values()].sort((a, b) => b.updatedAt - a.updatedAt);
  }

  replaceSnapshots(items: readonly SurfaceMemorySnapshot[], now = Date.now()): void {
    this.entries.clear();
    for (const item of items) {
      if (item.mode !== 'en' && item.mode !== 'zh') continue;
      if (!Number.isFinite(item.updatedAt)) continue;
      this.entries.set(item.key, { ...item });
    }
    this.prune(now);
  }

  clear(): void {
    this.entries.clear();
  }

  private prune(now: number): void {
    for (const [key, item] of this.entries) {
      if (now - item.updatedAt > this.options.maxAgeMs) this.entries.delete(key);
    }

    if (this.entries.size <= this.options.maxEntries) return;
    const keep = this.exportSnapshots().slice(0, this.options.maxEntries);
    this.entries.clear();
    for (const item of keep) this.entries.set(item.key, item);
  }
}

export function buildSurfaceMemoryKey(context: InputContext): string {
  return [
    normalize(context.appId),
    normalize(context.projectId),
    context.surface,
    normalize(context.controlId),
  ].join('|');
}

function normalize(value?: string): string {
  return value?.trim().toLowerCase() || '-';
}
