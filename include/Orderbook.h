#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

#include <nlib/common.h>
#include <nlib/hive.h>
#include <nlib/map.h>

namespace nq {

// Price-time-priority limit order book for one instrument, rebuilt by
// replaying a feed. A feed record is the book node: each resting nlib::order
// is copied into a nlib::hive, whose stable addresses let the order's own
// prev/next hooks chain the FIFO queue of its nlib::price_level. A std::map
// per side holds the levels in price order, and a nlib::map from order id to
// node makes trades and cancels O(1) lookups. The stored copy's qty is the
// remaining quantity. The book does not match: crossing orders rest until the
// feed reports their trades.
//
// One book runs on order flow or on L2 level records, never on both. Order
// flow builds the levels out of resting orders; OnLevel writes a level's
// total quantity straight in and leaves its order queue empty.
//
// Movable. Moving keeps the intrusive queues valid, because hive storage is
// address-stable.
class Orderbook {
 public:
  // Rests a limit-order add on its side of the book. Every other action,
  // every other order type, and an id already resting are dropped: a
  // well-formed feed never repeats an id, so a repeat is replay noise.
  void OnOrder(const nlib::order& o);

  // Applies one execution: both resting sides shrink by t.qty, and an order
  // leaves the book once nothing remains. Ids that never rested are ignored.
  void OnTrade(const nlib::trade& t);

  // Takes o.cancel_qty out of order o.order_id. The order leaves the book
  // once nothing remains. Unknown ids are ignored.
  void OnCancel(const nlib::order& o);

  // Sets order o.order_id's remaining quantity to o.new_qty. The order keeps
  // its queue position while o.price is unchanged, and goes to the back of
  // its new level otherwise; o.new_qty <= 0 removes it. Unknown ids are
  // ignored.
  void OnModify(const nlib::order& o);

  // Drops every resting order and every level, ahead of a feed snapshot
  // replay. o carries only the instrument and the times.
  void OnClear(const nlib::order& o);

  // Applies one absolute L2 record: sets the level's total quantity, creating
  // an aggregate level when absent; qty <= 0 erases the level. A book sees
  // either order-by-order events or level records, never both.
  void OnLevel(const nlib::level& l);

  // Aggregates the top nlib::book_depth price levels per side, best price
  // first, stamped with the event and receive times of the latest applied
  // event.
  nlib::book OnSnapshot() const;

  // Resting order count. An L2 book reports 0, because a level record rests
  // no order.
  std::size_t size() const { return by_id_.size(); }

  // Estimated bytes held by the book's storage, from the containers'
  // allocated capacities. Per-block bookkeeping adds a few percent on top.
  std::size_t MemoryBytes() const;

 private:
  // Keyed by price, ascending. Only OnSnapshot reads the ordering, so the
  // tree's O(log n) lookup buys ordered traversal for free; nlib has no
  // ordered container. Snapshots read bids in reverse for best-first.
  // Every stored level has qty > 0: a level whose quantity empties is erased.
  using PriceLevelMap = std::map<std::int64_t, nlib::price_level>;

  // Stamps later snapshots with instrument_id and the two event times.
  void Touch(std::uint32_t instrument_id, std::int64_t event_ns, std::int64_t recv_ns);

  // Links n to its price level, creating the level if absent.
  void LinkOrders(nlib::order* n);

  // Unlinks n from its price level, erasing the level if n was its last order.
  void UnlinkOrders(nlib::order* n);

  // Unlinks n and frees its storage and its id entry.
  void Erase(nlib::order* n);

  // Shrinks order order_id by qty. At nothing remaining the order leaves the
  // book. Unknown ids are ignored.
  void Reduce(std::int64_t order_id, std::int64_t qty);

  // Returns the level holding n. n must be linked.
  nlib::price_level& FindLevel(nlib::order* n);

  nlib::hive<nlib::order> orders_;
  nlib::map<std::int64_t, nlib::order*> by_id_;
  PriceLevelMap bids_;
  PriceLevelMap asks_;
  std::int64_t event_ns_ = 0;
  std::int64_t recv_ns_ = 0;
  std::uint32_t instrument_id_ = 0;
};

}
