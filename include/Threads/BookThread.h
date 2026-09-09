#pragma once

#include <chrono>
#include <cstdint>
#include <stop_token>

#include <Orderbook.h>
#include <Threads/FeedThread.h>
#include <Threads/MetricsThread.h>
#include <nlib/common.h>
#include <nlib/map.h>
#include <nlib/single_queue.h>

namespace nq {

using RecordQueue = nlib::single_queue<nlib::record>;

class BookThread {
public:
  BookThread(FeedQueue &in, RecordQueue &out,
             std::chrono::milliseconds snapshot_period)
      : feed_queue_(in), record_queue_(out), snapshot_period_(snapshot_period) {
  }

  void Run(Metrics &metrics, std::stop_token stop);

private:
  void Consume(nlib::feed_event &&e, Metrics &metrics);
  void Apply(const nlib::feed_event &e);

  // Forwards to writer thread
  void Forward(nlib::record &&r);

  // Forwards one snapshot per book
  void Snapshot(Metrics &metrics);

private:
  // ---------- Member variables ----------
  FeedQueue &feed_queue_;
  RecordQueue &record_queue_;
  std::chrono::milliseconds snapshot_period_;
  nlib::map<std::uint32_t, Orderbook> books_;
  std::uint64_t events_ = 0;
};

} // namespace nq
