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

using FeedQueue = nlib::single_queue<nlib::feed_event>;

class FeedThread {
public:
  FeedThread(std::string endpoint, bool l2, FeedQueue &out)
      : endpoint_(std::move(endpoint)), l2_(l2), sub_(ctx_, zmq::socket_type::sub),
        feed_queue_(out) {}

  void Run(Metrics &metrics, std::stop_token stop);

private:
  void Connect();

  bool Receive(zmq::message_t &msg);

  void Consume(const zmq::message_t &msg, Metrics &metrics, std::stop_token stop);

  void Forward(nlib::feed_event &&e, std::stop_token stop);

  std::string endpoint_;
  bool l2_;  // selects which frames Decode accepts
  zmq::context_t ctx_;
  zmq::socket_t sub_;
  FeedQueue &feed_queue_;
};

}
