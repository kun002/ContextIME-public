import { InputContext } from './types';

function safe(value: string | undefined): string {
  return value?.trim() || '-';
}

export function buildHabitContextKey(context: InputContext): string {
  return [
    safe(context.appId.toLowerCase()),
    context.surface,
    safe(context.languageId?.toLowerCase()),
    context.syntax,
    safe(context.projectId?.toLowerCase()),
    safe(context.controlId?.toLowerCase()),
  ].join('|');
}
