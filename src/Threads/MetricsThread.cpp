#include <Threads/MetricsThread.h>

#include <chrono>
#include <thread>

#include <nlib/common.h>

namespace nq {
namespace {

nlib::metrics Read(const Metrics& m) {
  return {
      .ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count(),
      .feed_messages = m.feed_messages.Read(),
      .feed_bytes = m.feed_bytes.Read(),
      .feed_orders = m.feed_orders.Read(),
      .feed_trades = m.feed_trades.Read(),
      .feed_levels = m.feed_levels.Read(),
      .feed_dropped = m.feed_dropped.Read(),
      .book_events = m.book_events.Read(),
      .book_apply_ns = m.book_apply_ns.Read(),
      .book_samples = m.book_samples.Read(),
      .book_instruments = m.book_instruments.Read(),
      .book_resting_orders = m.book_resting_orders.Read(),
      .book_memory_bytes = m.book_memory_bytes.Read(),
      .writer_orders = m.writer_orders.Read(),
      .writer_trades = m.writer_trades.Read(),
      .writer_levels = m.writer_levels.Read(),
      .writer_books = m.writer_books.Read(),
  };
}

}

void MetricsThread::Run(const Metrics& metrics, std::stop_token stop) {
  Bind();
  while (!stop.stop_requested()) {
    std::this_thread::sleep_for(sample_period_);
    Publish(metrics);
  }
}

void MetricsThread::Bind() {
  pub_.set(zmq::sockopt::conflate, 1);
  pub_.set(zmq::sockopt::linger, 0);
  pub_.bind(endpoint_);
}

void MetricsThread::Publish(const Metrics& metrics) {
  const nlib::metrics sample = Read(metrics);
  pub_.send(zmq::const_buffer(&sample, sizeof sample), zmq::send_flags::none);
}

}
