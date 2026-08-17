#include <Metrics.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <thread>

#include <zmq.hpp>

namespace nq {
namespace {

constexpr auto kSamplePeriod = std::chrono::seconds(1);

// How often the loop wakes to check `stop` between samples.
constexpr auto kStopPoll = std::chrono::milliseconds(100);

// The monotonic cells, captured at one instant; gauges are read at publish
// time instead, since a delta of a gauge means nothing.
struct Sample {
  std::uint64_t feed_messages, feed_bytes, feed_orders, feed_trades, feed_dropped;
  std::uint64_t book_events, book_apply_ns, book_samples;
  std::uint64_t writer_orders, writer_trades, writer_books;
};

Sample Read(const Metrics& m) {
  return {
      .feed_messages = m.feed_messages.Read(),
      .feed_bytes = m.feed_bytes.Read(),
      .feed_orders = m.feed_orders.Read(),
      .feed_trades = m.feed_trades.Read(),
      .feed_dropped = m.feed_dropped.Read(),
      .book_events = m.book_events.Read(),
      .book_apply_ns = m.book_apply_ns.Read(),
      .book_samples = m.book_samples.Read(),
      .writer_orders = m.writer_orders.Read(),
      .writer_trades = m.writer_trades.Read(),
      .writer_books = m.writer_books.Read(),
  };
}

// Formats one flat JSON object: per-second rates from the two samples, the
// window's average timed-apply latency, and the gauges as-is. Applies are
// timed 1 in 1024, so a quiet window may hold no timed apply; the average
// then falls back to the lifetime one.
std::string Line(const Metrics& m, const Sample& prev, const Sample& cur, double secs) {
  const auto rate = [secs](std::uint64_t a, std::uint64_t b) {
    return static_cast<double>(b - a) / secs;
  };
  const std::uint64_t samples = cur.book_samples - prev.book_samples;
  const std::uint64_t apply_avg_ns =
      samples          ? (cur.book_apply_ns - prev.book_apply_ns) / samples
      : cur.book_samples ? cur.book_apply_ns / cur.book_samples
                         : 0;
  const auto ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  return std::format(
      "{{\"ts_ns\":{},\"interval_s\":{:.3f},"
      "\"feed\":{{\"msg_per_s\":{:.1f},\"bytes_per_s\":{:.0f},\"order_per_s\":{:.1f},"
      "\"trade_per_s\":{:.1f},\"dropped\":{}}},"
      "\"book\":{{\"event_per_s\":{:.1f},\"apply_avg_ns\":{},\"instruments\":{},"
      "\"resting_orders\":{},\"memory_bytes\":{}}},"
      "\"writer\":{{\"order_per_s\":{:.1f},\"trade_per_s\":{:.1f},\"book_per_s\":{:.1f}}},"
      "\"total\":{{\"feed_messages\":{},\"book_events\":{},\"writer_rows\":{}}}}}",
      ts_ns, secs,
      rate(prev.feed_messages, cur.feed_messages), rate(prev.feed_bytes, cur.feed_bytes),
      rate(prev.feed_orders, cur.feed_orders), rate(prev.feed_trades, cur.feed_trades),
      cur.feed_dropped,
      rate(prev.book_events, cur.book_events), apply_avg_ns, m.book_instruments.Read(),
      m.book_resting_orders.Read(), m.book_memory_bytes.Read(),
      rate(prev.writer_orders, cur.writer_orders), rate(prev.writer_trades, cur.writer_trades),
      rate(prev.writer_books, cur.writer_books),
      cur.feed_messages, cur.book_events,
      cur.writer_orders + cur.writer_trades + cur.writer_books);
}

}  // namespace

void RunMetrics(const Metrics& m, const std::string& endpoint, std::stop_token stop) {
  zmq::context_t ctx;
  zmq::socket_t pub(ctx, zmq::socket_type::pub);
  pub.set(zmq::sockopt::linger, 0);
  pub.bind(endpoint);

  Sample prev = Read(m);
  auto prev_time = std::chrono::steady_clock::now();
  while (!stop.stop_requested()) {
    std::this_thread::sleep_for(kStopPoll);
    const auto now = std::chrono::steady_clock::now();
    if (now - prev_time < kSamplePeriod) continue;
    const Sample cur = Read(m);
    const double secs = std::chrono::duration<double>(now - prev_time).count();
    const std::string line = Line(m, prev, cur, secs);
    pub.send(zmq::buffer(line), zmq::send_flags::none);  // PUB drops, never blocks
    prev = cur;
    prev_time = now;
  }
}

}  // namespace nq
