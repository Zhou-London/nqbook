#pragma once

#include <chrono>
#include <filesystem>
#include <stop_token>
#include <string>
#include <variant>

#include <nlib/common.h>
#include <nlib/single_queue.h>

namespace nq {

// The nqbook process is three stages, one thread each: feed -> book -> writer.
// Adjacent stages share one nlib::single_queue, so every stage runs lock-free
// with exactly one producer and one consumer per queue.

// One wire record in flight from the feed stage to the book stage.
using FeedEvent = std::variant<nlib::order, nlib::trade>;

// One record in flight from the book stage to the writer stage: a forwarded
// feed record or a periodic book snapshot.
using Record = std::variant<nlib::order, nlib::trade, nlib::book>;

using FeedQueue = nlib::single_queue<FeedEvent>;
using RecordQueue = nlib::single_queue<Record>;

// Interval between the snapshots RunBook emits.
inline constexpr std::chrono::seconds kSnapshotPeriod{3};

// Wire framing accepted by RunFeed: one tag byte, then the record's bytes in
// host layout. The `order::prev`/`order::next` bytes are ignored on receive.
inline constexpr unsigned char kOrderTag = 0;
inline constexpr unsigned char kTradeTag = 1;

// Overload set builder for std::visit dispatch over the event variants.
template <typename... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};

// Receives framed records on a ZMQ SUB socket connected to `endpoint` and
// pushes them into `out`; messages that match no framing are dropped. Waits
// while `out` is full, so a slow book stage backpressures into ZMQ. Returns
// once `stop` is requested.
void RunFeed(const std::string& endpoint, FeedQueue& out, std::stop_token stop);

// Applies each event from `in` to an Orderbook and forwards the raw event
// into `out`, interleaving an OnSnapshot() record every kSnapshotPeriod. On
// stop it drains `in` before returning, so no received event is lost.
void RunBook(FeedQueue& in, RecordQueue& out, std::stop_token stop);

// Appends records from `in` to one Parquet file per record type under `dir`
// (created if missing), names stamped with the writer start time. Rows are
// written a batch at a time, one row group per batch. On stop it drains `in`,
// flushes partial batches, and closes the files; a storage error aborts the
// process, since running on without persistence would silently lose data.
void RunWriter(RecordQueue& in, const std::filesystem::path& dir, std::stop_token stop);

}  // namespace nq
