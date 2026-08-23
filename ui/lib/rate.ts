// Sliding-window rate estimation over wall-clock arrival times. Counters in
// the stream are cumulative; the page differences consecutive samples and
// feeds each increment here, stamped with Date.now() at arrival.

/** Window length: rates are means over the last second of wall time. */
export const RATE_WINDOW_MS = 1000;

/**
 * Sums the increments that arrived in the trailing RATE_WINDOW_MS.
 *
 * A deque of (arrival timestamp, increment) pairs: push appends at the tail,
 * and every push or read first evicts expired entries from the head while
 * adjusting the running sum. Both are amortized O(1); memory is bounded by
 * one window of entries. The deque is an array plus a head index — the
 * evicted prefix is sliced off once it dominates, so no per-pop shifting.
 */
export class SlidingRate {
  private entries: { t: number; v: number }[] = [];
  private head = 0;
  private sum = 0;

  push(t: number, v: number): void {
    this.entries.push({ t, v });
    this.sum += v;
    this.evict(t);
  }

  /** Windowed sum: increments that arrived in (now - RATE_WINDOW_MS, now]. */
  total(now: number): number {
    this.evict(now);
    return this.sum;
  }

  private evict(now: number): void {
    const cutoff = now - RATE_WINDOW_MS;
    const entries = this.entries;
    while (this.head < entries.length && entries[this.head].t <= cutoff) {
      this.sum -= entries[this.head].v;
      this.head += 1;
    }
    if (this.head > 32 && this.head * 2 >= entries.length) {
      this.entries = entries.slice(this.head);
      this.head = 0;
    }
  }
}
