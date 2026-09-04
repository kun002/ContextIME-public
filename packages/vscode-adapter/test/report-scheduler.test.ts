import assert from 'node:assert/strict';
import test from 'node:test';
import { ReportScheduler } from '../src/report-scheduler';

const wait = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

test('scheduler coalesces rapid events and keeps the newest trigger', async () => {
  const calls: string[] = [];
  const scheduler = new ReportScheduler(async (trigger) => {
    calls.push(trigger);
  }, () => 5);

  scheduler.schedule('selection');
  scheduler.schedule('document');
  scheduler.schedule('terminal');
  await wait(20);

  assert.deepEqual(calls, ['terminal']);
  scheduler.dispose();
});

test('scheduler runs a pending event after the current report finishes', async () => {
  const calls: string[] = [];
  let resolveSecond!: () => void;
  const secondCompleted = new Promise<void>((resolve) => {
    resolveSecond = resolve;
  });
  const scheduler = new ReportScheduler(async (trigger) => {
    calls.push(trigger);
    if (trigger === 'first') {
      scheduler.schedule('second');
      await wait(10);
    } else if (trigger === 'second') {
      resolveSecond();
    }
  }, () => 0);

  scheduler.schedule('first');
  await new Promise<void>((resolve, reject) => {
    const timeout = setTimeout(
      () => reject(new Error('second report timed out')),
      500,
    );
    void secondCompleted.then(() => {
      clearTimeout(timeout);
      resolve();
    });
  });

  assert.deepEqual(calls, ['first', 'second']);
  scheduler.dispose();
});
