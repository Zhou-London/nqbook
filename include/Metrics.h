#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <string>

namespace nq {

// A cache line on every target this service runs on; a fixed constant rather
// than std::hardware_destructive_interference_size so the padding is part of
// the type's contract, not a tuning flag.
inline constexpr std::size_t kMetricCacheLine = 64;

// One 64-bit metric cell with exactly one writing thread; any thread may
// read. Updates are a plain load + store with relaxed ordering — no
// read-modify-write, so a hot-path Add costs about a nanosecond — and each
// cell owns a full cache line, so cells never false-share with each other or
// with the writer's hot data.
struct alignas(kMetricCacheLine) Metric {
  void Add(std::uint64_t n = 1) {
    value.store(value.load(std::memory_order_relaxed) + n, std::memory_order_relaxed);
  }
  void Set(std::uint64_t n) { value.store(n, std::memory_order_relaxed); }
  std::uint64_t Read() const { return value.load(std::memory_order_relaxed); }

  std::atomic<std::uint64_t> value{0};
};

static_assert(sizeof(Metric) == kMetricCacheLine);

// The cells the pipeline's hot paths write and RunMetrics samples. Every
// cell has exactly one writer — feed_* the feed thread, book_* the book
// thread, writer_* the writer thread — and is monotonic unless noted as a
// gauge.
struct Metrics {
  Metric feed_messages;  // ZMQ messages received, matched or not
  Metric feed_bytes;     // payload bytes of those messages
  Metric feed_orders;    // decoded order frames
  Metric feed_trades;    // decoded trade frames
  Metric feed_dropped;   // messages matching no framing

  Metric book_events;          // events applied across all books
  Metric book_apply_ns;        // cumulative latency of the timed applies
  Metric book_samples;         // applies actually timed (1 in 1024)
  Metric book_instruments;     // gauge: books held
  Metric book_resting_orders;  // gauge: resting orders across books
  Metric book_memory_bytes;    // gauge: estimated book storage

  Metric writer_orders;  // order rows appended
  Metric writer_trades;  // trade rows appended
  Metric writer_books;   // book snapshot rows appended
};

// Samples `m` every 100 ms and immediately publishes each sample as one raw
// nlib::metrics record on a PUB socket bound to `endpoint`; the socket and
// its context live entirely on this thread. The socket conflates
// (ZMQ_CONFLATE): nothing queues beyond the newest sample, so a subscriber
// always reads live state, never a backlog. Counters are cumulative —
// consumers difference consecutive samples for rates. The thread never reads
// pipeline data, only these cells. Returns once `stop` is requested.
void RunMetrics(const Metrics& m, const std::string& endpoint, std::stop_token stop);

}  // namespace nq
