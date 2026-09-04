export type InputMode = 'en' | 'zh';

export type InputSurface =
  | 'editor'
  | 'terminal'
  | 'search'
  | 'commandPalette'
  | 'chat'
  | 'commit'
  | 'addressBar'
  | 'webText'
  | 'unknown';

export type SyntaxContext =
  | 'code'
  | 'comment'
  | 'string'
  | 'markdownText'
  | 'markdownCode'
  | 'unknown';

export interface InputContext {
  appId: string;
  surface: InputSurface;
  syntax: SyntaxContext;
  currentMode: InputMode;
  languageId?: string;
  projectId?: string;
  controlId?: string;
  compositionActive?: boolean;
  remoteSession?: boolean;
  automationPaused?: boolean;
  lockedMode?: InputMode | null;
  manualOverrideUntil?: number;
}

export type DecisionSource =
  | 'paused'
  | 'lock'
  | 'composition'
  | 'manual'
  | 'habit'
  | 'syntax'
  | 'memory'
  | 'surface'
  | 'application'
  | 'fallback';

export interface ModeDecision {
  targetMode: InputMode;
  shouldSwitch: boolean;
  source: DecisionSource;
  reason: string;
  confidence: number;
}

export interface HabitPreference {
  mode: InputMode;
  confidence: number;
  samples: number;
}

export interface HabitLookup {
  lookup(context: InputContext, now?: number): HabitPreference | null;
}

export interface SurfaceMemoryLookup {
  lookup(context: InputContext, now?: number): InputMode | null;
}

export interface PolicyConfig {
  appDefaults: Readonly<Record<string, InputMode>>;
  surfaceDefaults: Readonly<Partial<Record<InputSurface, InputMode>>>;
  habitMinSamples: number;
  habitConfidenceThreshold: number;
}
