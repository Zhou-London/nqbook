#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stop_token>
#include <string>
#include <utility>

#include <zmq.hpp>

#include <nlib/common.h>

namespace nq {

struct alignas(NLIB_CACHE_LINE) Metric {
  void Add(std::uint64_t n = 1) {
    value.store(value.load(std::memory_order_relaxed) + n,
                std::memory_order_relaxed);
  }
  void Set(std::uint64_t n) { value.store(n, std::memory_order_relaxed); }
  std::uint64_t Read() const { return value.load(std::memory_order_relaxed); }

  std::atomic<std::uint64_t> value{0};
};

static_assert(sizeof(Metric) == NLIB_CACHE_LINE);

struct Metrics {
  Metric feed_messages; // ZMQ messages received, matched or not
  Metric feed_bytes;    // payload bytes of those messages
  Metric feed_orders;   // decoded order frames
  Metric feed_trades;   // decoded trade frames
  Metric feed_levels;   // decoded level frames
  Metric feed_dropped;  // messages the framing rejected

  Metric book_events;         // events applied across all books
  Metric book_apply_ns;       // cumulative latency of the timed applies
  Metric book_samples;        // applies actually timed (1 in 1024)
  Metric book_instruments;    // gauge: books held
  Metric book_resting_orders; // gauge: resting orders across books
  Metric book_memory_bytes;   // gauge: estimated book storage

  Metric writer_orders; // order rows appended
  Metric writer_trades; // trade rows appended
  Metric writer_levels; // level rows appended
  Metric writer_books;  // book snapshot rows appended
};

class MetricsThread {
public:
  MetricsThread(std::string endpoint, std::chrono::milliseconds sample_period)
      : endpoint_(std::move(endpoint)), pub_(ctx_, zmq::socket_type::pub),
        sample_period_(sample_period) {}

  void Run(const Metrics &metrics, std::stop_token stop);

private:
  // ---------- Member variables ----------
  void Bind();
  void Publish(const Metrics &metrics);

  std::string endpoint_;
  zmq::context_t ctx_;
  zmq::socket_t pub_;
  std::chrono::milliseconds sample_period_;
};

} // namespace nq
