// Decodes nlib::metrics — the 136-byte little-endian record nqbook's metrics
// thread publishes — into a plain object. Counters are cumulative since
// process start; consumers difference consecutive samples for rates.

export const METRICS_SIZE = 136;

export interface MetricsSample {
  /** Sample time, Unix-epoch nanoseconds. Rounded through a double, so good
   * to ~microseconds — plenty for rate math and display. */
  tsNs: number;
  feedMessages: number;
  feedBytes: number;
  feedOrders: number;
  feedTrades: number;
  feedLevels: number;
  feedDropped: number;
  bookEvents: number;
  bookApplyNs: number;
  bookSamples: number;
  bookInstruments: number;
  bookRestingOrders: number;
  bookMemoryBytes: number;
  writerOrders: number;
  writerTrades: number;
  writerLevels: number;
  writerBooks: number;
}

export function decode(frame: Buffer): MetricsSample {
  const u = (offset: number) => Number(frame.readBigUInt64LE(offset));
  return {
    tsNs: Number(frame.readBigInt64LE(0)),
    feedMessages: u(8),
    feedBytes: u(16),
    feedOrders: u(24),
    feedTrades: u(32),
    feedLevels: u(40),
    feedDropped: u(48),
    bookEvents: u(56),
    bookApplyNs: u(64),
    bookSamples: u(72),
    bookInstruments: u(80),
    bookRestingOrders: u(88),
    bookMemoryBytes: u(96),
    writerOrders: u(104),
    writerTrades: u(112),
    writerLevels: u(120),
    writerBooks: u(128),
  };
}
