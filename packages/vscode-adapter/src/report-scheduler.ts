/**
 * Trailing scheduler that coalesces high-frequency editor events while
 * guaranteeing that a newer report is sent after an in-flight report ends.
 */
export class ReportScheduler {
  private timer: NodeJS.Timeout | null = null;
  private running = false;
  private pendingTrigger: string | null = null;
  private disposed = false;

  constructor(
    private readonly run: (trigger: string) => Promise<void>,
    private readonly delayProvider: () => number,
  ) {}

  schedule(trigger: string): void {
    if (this.disposed) return;
    this.pendingTrigger = trigger;
    if (this.timer) clearTimeout(this.timer);
    this.timer = setTimeout(() => {
      this.timer = null;
      void this.flush();
    }, Math.max(0, this.delayProvider()));
  }

  async flush(): Promise<void> {
    if (this.disposed || this.running) return;
    const trigger = this.pendingTrigger;
    if (!trigger) return;

    this.pendingTrigger = null;
    this.running = true;
    try {
      await this.run(trigger);
    } finally {
      this.running = false;
      if (this.pendingTrigger) this.schedule(this.pendingTrigger);
    }
  }

  dispose(): void {
    this.disposed = true;
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
    this.pendingTrigger = null;
  }
}
