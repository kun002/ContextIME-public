export * from './types';
export * from './runtime';
export * from './native-port';

import { KoffiWindowsNativePort } from './native-port';
import { WindowsImeRuntime } from './runtime';
import { RuntimeLogger, WindowsImeRuntimeConfig } from './types';

export function createWindowsImeRuntime(
  logger: RuntimeLogger,
  config: Partial<WindowsImeRuntimeConfig> = {},
): WindowsImeRuntime {
  const runtime = new WindowsImeRuntime(new KoffiWindowsNativePort(), logger, config);
  runtime.start();
  return runtime;
}
