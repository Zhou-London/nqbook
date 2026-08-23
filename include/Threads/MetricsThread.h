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

inline constexpr std::size_t kMetricCacheLine = 64;

struct alignas(kMetricCacheLine) Metric {
  void Add(std::uint64_t n = 1) {
    value.store(value.load(std::memory_order_relaxed) + n, std::memory_order_relaxed);
  }
  void Set(std::uint64_t n) { value.store(n, std::memory_order_relaxed); }
  std::uint64_t Read() const { return value.load(std::memory_order_relaxed); }

  std::atomic<std::uint64_t> value{0};
};

static_assert(sizeof(Metric) == kMetricCacheLine);

struct Metrics {
  Metric feed_messages;
  Metric feed_bytes;
  Metric feed_orders;
  Metric feed_trades;
  Metric feed_levels;
  Metric feed_dropped;

  Metric book_events;
  Metric book_apply_ns;
  Metric book_samples;
  Metric book_instruments;
  Metric book_resting_orders;
  Metric book_memory_bytes;

  Metric writer_orders;
  Metric writer_trades;
  Metric writer_levels;
  Metric writer_books;
};

class MetricsThread {
public:
  MetricsThread(std::string endpoint, std::chrono::milliseconds sample_period)
      : endpoint_(std::move(endpoint)), pub_(ctx_, zmq::socket_type::pub),
        sample_period_(sample_period) {}

  void Run(const Metrics &metrics, std::stop_token stop);

private:
  void Bind();

  void Publish(const Metrics &metrics);

  std::string endpoint_;
  zmq::context_t ctx_;
  zmq::socket_t pub_;
  std::chrono::milliseconds sample_period_;
};

}
