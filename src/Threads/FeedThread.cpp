#include <Threads/FeedThread.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

#include <nlib/common.h>

namespace nq {
namespace {

constexpr int kRecvTimeoutMs = 100;

std::int64_t RecvTimeNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// Decodes one framed record, gated by the process modality: with l2 the feed
// accepts level, trade, and clear records; without it, everything but level
// records. Rejected frames count as dropped.
std::optional<nlib::feed_event> Decode(const zmq::message_t& msg, std::int64_t recv_ns,
                                       bool l2) {
  const auto* bytes = static_cast<const unsigned char*>(msg.data());
  if (msg.size() == 1 + sizeof(nlib::order) && bytes[0] == nlib::order_tag) {
    nlib::order o;
    std::memcpy(&o, bytes + 1, sizeof o);
    if (l2 && o.action != nlib::order_action::clear) return std::nullopt;
    o.prev = o.next = nullptr;
    o.recv_ns = recv_ns;
    return o;
  }
  if (msg.size() == 1 + sizeof(nlib::trade) && bytes[0] == nlib::trade_tag) {
    nlib::trade t;
    std::memcpy(&t, bytes + 1, sizeof t);
    t.recv_ns = recv_ns;
    return t;
  }
  if (msg.size() == 1 + sizeof(nlib::level) && bytes[0] == nlib::level_tag) {
    if (!l2) return std::nullopt;
    nlib::level l;
    std::memcpy(&l, bytes + 1, sizeof l);
    l.recv_ns = recv_ns;
    return l;
  }
  return std::nullopt;
}

}

void FeedThread::Run(Metrics& metrics, std::stop_token stop) {
  Connect();
  zmq::message_t msg;
  while (!stop.stop_requested()) {
    if (Receive(msg)) Consume(msg, metrics, stop);
  }
}

void FeedThread::Connect() {
  sub_.set(zmq::sockopt::subscribe, "");
  sub_.set(zmq::sockopt::rcvtimeo, kRecvTimeoutMs);
  sub_.set(zmq::sockopt::linger, 0);
  sub_.connect(endpoint_);
}

bool FeedThread::Receive(zmq::message_t& msg) {
  try {
    return sub_.recv(msg, zmq::recv_flags::none).has_value();
  } catch (const zmq::error_t& e) {
    if (e.num() == EINTR) return false;
    throw;
  }
}

void FeedThread::Consume(const zmq::message_t& msg, Metrics& metrics, std::stop_token stop) {
  metrics.feed_messages.Add();
  metrics.feed_bytes.Add(msg.size());
  std::optional<nlib::feed_event> e = Decode(msg, RecvTimeNs(), l2_);
  if (!e) {
    metrics.feed_dropped.Add();
    return;
  }
  (std::holds_alternative<nlib::order>(*e)   ? metrics.feed_orders
   : std::holds_alternative<nlib::trade>(*e) ? metrics.feed_trades
                                             : metrics.feed_levels)
      .Add();
  Forward(std::move(*e), stop);
}

void FeedThread::Forward(nlib::feed_event&& e, std::stop_token stop) {
  while (!feed_queue_.try_push(std::move(e))) {
    if (stop.stop_requested()) return;
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
}

}
