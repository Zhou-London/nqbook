#pragma once

#include <optional>
#include <stop_token>
#include <string>
#include <utility>

#include <zmq.hpp>

#include <Threads/MetricsThread.h>
#include <nlib/common.h>
#include <nlib/single_queue.h>

namespace nq {

// One wire record in flight from the feed stage to the book stage.
using FeedQueue = nlib::single_queue<nlib::feed_event>;

// The feed stage: receives framed records on a ZMQ SUB socket and pushes them
// into the FeedQueue the book stage drains. A frame is one nlib tag byte
// followed by the record in host layout; anything matching no tag-plus-size
// is dropped and counted. The order::prev and order::next bytes arrive on the
// wire and are discarded, because the hooks are book-owned state.
class FeedThread {
public:
  // Connects to endpoint and pushes into out. l2 selects level records over
  // order flow.
  FeedThread(std::string endpoint, bool l2, FeedQueue &out)
      : endpoint_(std::move(endpoint)), l2_(l2), sub_(ctx_, zmq::socket_type::sub),
        feed_queue_(out) {}

  // Receives until stop is requested, stamping each record's recv_ns with its
  // receive time. A full queue makes this stage wait, so a slow book stage
  // backpressures into ZMQ.
  void Run(Metrics &metrics, std::stop_token stop);

private:
  // Subscribes to every topic and connects the socket.
  void Connect();

  // Reads one message, or returns false at the receive timeout.
  bool Receive(zmq::message_t &msg);

  // Decodes one message and forwards it. Counts the message, its bytes, and
  // either its record type or the drop.
  void Consume(const zmq::message_t &msg, Metrics &metrics, std::stop_token stop);

  // Pushes e into the queue, waiting while the queue is full.
  void Forward(nlib::feed_event &&e, std::stop_token stop);

  std::string endpoint_;
  bool l2_;  // selects which frames Decode accepts
  zmq::context_t ctx_;
  zmq::socket_t sub_;
  FeedQueue &feed_queue_;
};

}
