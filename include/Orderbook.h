#pragma once

#include <cstdint>

#include <nlib/common.h>
#include <nlib/hive.h>
#include <nlib/map.h>

namespace nq {

// Price-time-priority limit order book for one instrument, rebuilt by
// replaying an order-by-order feed. Feed records are the book nodes: each
// resting nlib::order is copied into a nlib::hive, whose stable addresses let
// the order's own prev/next hooks chain one intrusive list per side, best
// price first and arrival order within a price; a nlib::map from order id to
// order makes trades and cancels O(1) lookups. The stored copy's qty tracks
// the remaining quantity. The book does not match: crossing orders rest until
// the feed reports their trades.
class Orderbook {
 public:
  // Rests a limit-order add on its side of the book. Anything else is
  // dropped: market orders cannot rest without matching, and cancels arrive
  // through OnCancel().
  void OnOrder(const nlib::order& o);

  // Applies an execution: both resting sides shrink by t.qty and leave the
  // book once fully filled. Ids that never rested are skipped.
  void OnTrade(const nlib::trade& t);

  // Applies a cancel: order o.order_id shrinks by the cancelled quantity and
  // leaves the book at zero remaining.
  void OnCancel(const nlib::order& o);

  // Aggregates the top nlib::book_depth price levels per side, stamped with
  // the latest event time seen.
  nlib::book OnSnapshot() const;

 private:
  // Stamps later snapshots with `instrument_id` and `time_ns`.
  void Touch(std::uint32_t instrument_id, std::int64_t time_ns);

  // Links `n` into its side's list at its price-time position.
  void Link(nlib::order* n);

  // Shrinks order `order_id` by `qty`; at zero remaining it leaves the book
  // and its storage and id entry are freed. Unknown ids are ignored.
  void Reduce(std::int64_t order_id, std::int64_t qty);

  // Writes up to nlib::book_depth (price, summed qty) levels from the side
  // list starting at `n`; trailing levels stay zero.
  static void FillSide(const nlib::order* n, std::int64_t* price, std::int64_t* qty);

  nlib::hive<nlib::order> orders_;
  nlib::map<std::int64_t, nlib::order*> by_id_;
  nlib::order* bid_head_ = nullptr;  // highest bid first
  nlib::order* ask_head_ = nullptr;  // lowest ask first
  std::int64_t time_ns_ = 0;         // latest event time
  std::uint32_t instrument_id_ = 0;
};

}  // namespace nq
