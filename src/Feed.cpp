#include <Pipeline.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <thread>
#include <variant>

#include <zmq.hpp>

namespace nq {
namespace {

// Copies a framed wire record out of `msg` and stamps its recv_ns with
// `recv_ns`; returns std::nullopt when neither tag-plus-size matches. The
// order's list hooks are book-owned state, so the wire bytes under them are
// discarded.
std::optional<FeedEvent> Decode(const zmq::message_t& msg, std::int64_t recv_ns) {
  const auto* bytes = static_cast<const unsigned char*>(msg.data());
  if (msg.size() == 1 + sizeof(nlib::order) && bytes[0] == kOrderTag) {
    nlib::order o;
    std::memcpy(&o, bytes + 1, sizeof o);
    o.prev = o.next = nullptr;
    o.recv_ns = recv_ns;
    return o;
  }
  if (msg.size() == 1 + sizeof(nlib::trade) && bytes[0] == kTradeTag) {
    nlib::trade t;
    std::memcpy(&t, bytes + 1, sizeof t);
    t.recv_ns = recv_ns;
    return t;
  }
  return std::nullopt;
}

}  // namespace

void RunFeed(const std::string& endpoint, FeedQueue& out, Metrics& metrics,
             std::stop_token stop) {
  zmq::context_t ctx;
  zmq::socket_t sub(ctx, zmq::socket_type::sub);
  sub.set(zmq::sockopt::subscribe, "");
  sub.set(zmq::sockopt::rcvtimeo, 100);  // ms; bounds how long stop can go unnoticed
  sub.set(zmq::sockopt::linger, 0);
  sub.connect(endpoint);

  zmq::message_t msg;
  while (!stop.stop_requested()) {
    zmq::recv_result_t received;
    try {
      received = sub.recv(msg, zmq::recv_flags::none);
    } catch (const zmq::error_t& e) {
      if (e.num() == EINTR) continue;  // signal during recv; the stop check decides
      throw;
    }
    if (!received) continue;  // timeout
    metrics.feed_messages.Add();
    metrics.feed_bytes.Add(msg.size());
    // system_clock, so the stamp shares the exchange event times' epoch.
    const std::int64_t recv_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
    std::optional<FeedEvent> event = Decode(msg, recv_ns);
    if (!event) {
      metrics.feed_dropped.Add();
      continue;
    }
    (std::holds_alternative<nlib::order>(*event) ? metrics.feed_orders : metrics.feed_trades)
        .Add();
    while (!out.try_push(std::move(*event))) {  // full: wait for the book to catch up
      if (stop.stop_requested()) return;
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  }
}

}  // namespace nq
