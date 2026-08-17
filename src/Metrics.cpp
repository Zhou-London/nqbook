#include <Metrics.h>

#include <chrono>
#include <thread>

#include <nlib/common.h>
#include <zmq.hpp>

namespace nq {
namespace {

// The publish cadence, and how long a stop request can go unnoticed.
constexpr auto kSamplePeriod = std::chrono::milliseconds(100);

nlib::metrics Read(const Metrics& m) {
  return {
      .ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count(),
      .feed_messages = m.feed_messages.Read(),
      .feed_bytes = m.feed_bytes.Read(),
      .feed_orders = m.feed_orders.Read(),
      .feed_trades = m.feed_trades.Read(),
      .feed_dropped = m.feed_dropped.Read(),
      .book_events = m.book_events.Read(),
      .book_apply_ns = m.book_apply_ns.Read(),
      .book_samples = m.book_samples.Read(),
      .book_instruments = m.book_instruments.Read(),
      .book_resting_orders = m.book_resting_orders.Read(),
      .book_memory_bytes = m.book_memory_bytes.Read(),
      .writer_orders = m.writer_orders.Read(),
      .writer_trades = m.writer_trades.Read(),
      .writer_books = m.writer_books.Read(),
  };
}

}  // namespace

void RunMetrics(const Metrics& m, const std::string& endpoint, std::stop_token stop) {
  zmq::context_t ctx;
  zmq::socket_t pub(ctx, zmq::socket_type::pub);
  // Conflation keeps only the newest sample per subscriber: a slow or absent
  // reader never accumulates a backlog, and every delivery is current.
  pub.set(zmq::sockopt::conflate, 1);
  pub.set(zmq::sockopt::linger, 0);
  pub.bind(endpoint);

  while (!stop.stop_requested()) {
    std::this_thread::sleep_for(kSamplePeriod);
    const nlib::metrics sample = Read(m);
    pub.send(zmq::const_buffer(&sample, sizeof sample), zmq::send_flags::none);
  }
}

}  // namespace nq
