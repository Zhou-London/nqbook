#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <string>
#include <utility>

#include <zmq.hpp>

namespace nq {

// A cache line on every target this service runs on. A fixed constant rather
// than std::hardware_destructive_interference_size, so the padding is part of
// the type's contract instead of a tuning flag.
inline constexpr std::size_t kMetricCacheLine = 64;

// One 64-bit metric cell with exactly one writing thread; any thread may
// read. An update is a plain load plus store with relaxed ordering, so a
// hot-path Add costs about a nanosecond. Each cell owns a full cache line, so
// cells never false-share with each other or with the writer's hot data.
struct alignas(kMetricCacheLine) Metric {
  void Add(std::uint64_t n = 1) {
    value.store(value.load(std::memory_order_relaxed) + n, std::memory_order_relaxed);
  }
  void Set(std::uint64_t n) { value.store(n, std::memory_order_relaxed); }
  std::uint64_t Read() const { return value.load(std::memory_order_relaxed); }

  std::atomic<std::uint64_t> value{0};
};

static_assert(sizeof(Metric) == kMetricCacheLine);

// The cells the pipeline's hot paths write and the metrics stage samples.
// Every cell has exactly one writer — feed_* the feed thread, book_* the book
// thread, writer_* the writer thread — and counts up, unless marked a gauge.
struct Metrics {
  Metric feed_messages;  // ZMQ messages received, matched or not
  Metric feed_bytes;     // payload bytes of those messages
  Metric feed_orders;    // decoded order frames
  Metric feed_trades;    // decoded trade frames
  Metric feed_levels;    // decoded level frames
  Metric feed_dropped;   // messages the framing rejected

  Metric book_events;          // events applied across all books
  Metric book_apply_ns;        // cumulative latency of the timed applies
  Metric book_samples;         // applies actually timed (1 in 1024)
  Metric book_instruments;     // gauge: books held
  Metric book_resting_orders;  // gauge: resting orders across books
  Metric book_memory_bytes;    // gauge: estimated book storage

  Metric writer_orders;  // order rows appended
  Metric writer_trades;  // trade rows appended
  Metric writer_levels;  // level rows appended
  Metric writer_books;   // book snapshot rows appended
};

// The metrics stage: samples the cells and publishes each sample as one raw
// nlib::metrics record on its own ZMQ PUB socket. The socket and its context
// live entirely on this thread, and the thread reads no pipeline data and
// takes no lock.
class MetricsThread {
public:
  // Binds endpoint and publishes one sample every sample_period.
  MetricsThread(std::string endpoint, std::chrono::milliseconds sample_period)
      : endpoint_(std::move(endpoint)), pub_(ctx_, zmq::socket_type::pub),
        sample_period_(sample_period) {}

  // Publishes until stop is requested. Counters are cumulative, so a
  // subscriber differences consecutive samples for rates.
  void Run(const Metrics &metrics, std::stop_token stop);

private:
  // Binds the socket with ZMQ_CONFLATE set: nothing queues beyond the newest
  // sample, so a subscriber always reads live state rather than a backlog.
  void Bind();

  // Sends one sample of every cell, stamped with the current time.
  void Publish(const Metrics &metrics);

  std::string endpoint_;
  zmq::context_t ctx_;
  zmq::socket_t pub_;
  std::chrono::milliseconds sample_period_;
};

}
