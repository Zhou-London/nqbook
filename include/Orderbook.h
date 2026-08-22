#pragma once

#include <cstddef>
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
// the feed reports their trades. Movable; moving keeps the intrusive lists
// valid because hive storage is address-stable.
class Orderbook {
 public:
  // Rests a limit-order add on its side of the book. Market orders cannot
  // rest without matching and ids already resting are dropped: a well-formed
  // feed never repeats an id, so a repeat is replay noise.
  void OnOrder(const nlib::order& o);

  // Applies an execution: both resting sides shrink by t.qty and leave the
  // book once fully filled. Ids that never rested are skipped.
  void OnTrade(const nlib::trade& t);

  // Takes o.cancel_qty out of order o.order_id; at nothing remaining the
  // order leaves the book. Unknown ids are ignored.
  void OnCancel(const nlib::order& o);

  // Sets order o.order_id's remaining quantity to o.new_qty, keeping its
  // queue position while o.price is unchanged and re-queueing it at the back
  // of its new price level otherwise; o.new_qty <= 0 removes it. Unknown ids
  // are ignored.
  void OnModify(const nlib::order& o);

  // Drops every resting order; `o` carries only the times and instrument.
  void OnClear(const nlib::order& o);

  // Aggregates the top nlib::book_depth price levels per side, stamped with
  // the event and receive times of the latest event applied.
  nlib::book OnSnapshot() const;

  // Resting order count.
  std::size_t size() const { return by_id_.size(); }

  // Estimated bytes held by the book's storage, from the containers'
  // allocated capacities; per-block bookkeeping adds a few percent on top.
  std::size_t MemoryBytes() const;

 private:
  // Stamps later snapshots with `instrument_id` and the event times.
  void Touch(std::uint32_t instrument_id, std::int64_t event_ns, std::int64_t recv_ns);

  // Links `n` into its side's list at its price-time position.
  void Link(nlib::order* n);

  // Removes `n` from its side's list; storage and id entry stay.
  void Unlink(nlib::order* n);

  // Unlinks `n` and frees its storage and id entry.
  void Erase(nlib::order* n);

  // Shrinks order `order_id` by `qty`; at zero remaining it leaves the book.
  // Unknown ids are ignored.
  void Reduce(std::int64_t order_id, std::int64_t qty);

  // Writes up to nlib::book_depth (price, summed qty) levels from the side
  // list starting at `n`; trailing levels stay zero.
  static void FillSide(const nlib::order* n, std::int64_t* price, std::int64_t* qty);

  nlib::hive<nlib::order> orders_;
  nlib::map<std::int64_t, nlib::order*> by_id_;
  nlib::order* bid_head_ = nullptr;  // highest bid first
  nlib::order* ask_head_ = nullptr;  // lowest ask first
  std::int64_t event_ns_ = 0;        // event time of the latest applied event
  std::int64_t recv_ns_ = 0;         // receive time of the latest applied event
  std::uint32_t instrument_id_ = 0;
};

}  // namespace nq
