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

// One record in flight from the book stage to the writer stage: a forwarded
// feed record, or a periodic book snapshot.
using RecordQueue = nlib::single_queue<nlib::record>;

// The book stage: applies each feed event to its instrument's Orderbook,
// created on first sight, and forwards the event into the RecordQueue the
// writer stage drains. Snapshots of every book are interleaved once per
// snapshot period, on the wall clock, whether or not a book saw an event.
class BookThread {
public:
  // Drains in, pushes into out, and snapshots every snapshot_period.
  BookThread(FeedQueue &in, RecordQueue &out, std::chrono::milliseconds snapshot_period)
      : feed_queue_(in), record_queue_(out), snapshot_period_(snapshot_period) {}

  // Applies events until stop is requested, then drains in before returning,
  // so no received event is lost.
  void Run(Metrics &metrics, std::stop_token stop);

private:
  // Applies e and forwards it. Counts the event, and times every 1024th
  // apply.
  void Consume(nlib::feed_event &&e, Metrics &metrics);

  // Routes e to its instrument's book, by record type and order action.
  void Apply(const nlib::feed_event &e);

  // Pushes r into the queue, waiting while the queue is full.
  void Forward(nlib::record &&r);

  // Forwards one snapshot per book and refreshes the book gauges.
  void Snapshot(Metrics &metrics);

  FeedQueue &feed_queue_;
  RecordQueue &record_queue_;
  std::chrono::milliseconds snapshot_period_;
  nlib::map<std::uint32_t, Orderbook> books_;
  std::uint64_t events_ = 0;
};

}
