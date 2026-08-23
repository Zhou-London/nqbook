#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

#include <nlib/common.h>
#include <nlib/hive.h>
#include <nlib/map.h>

namespace nq {

class Orderbook {
 public:
  void OnOrder(const nlib::order& o);

  void OnTrade(const nlib::trade& t);

  void OnCancel(const nlib::order& o);

  void OnModify(const nlib::order& o);

  void OnClear(const nlib::order& o);

  // Applies one absolute L2 record: sets the level's total quantity, creating
  // an aggregate level when absent; qty <= 0 erases the level. A book sees
  // either order-by-order events or level records, never both.
  void OnLevel(const nlib::level& l);

  nlib::book OnSnapshot() const;

  std::size_t size() const { return by_id_.size(); }

  std::size_t MemoryBytes() const;

 private:
  // Keyed by price, ascending. Only OnSnapshot reads the ordering, so the
  // tree's O(log n) lookup buys ordered traversal for free; nlib has no
  // ordered container. Snapshots read bids in reverse for best-first.
  // Every stored level has qty > 0: a level whose quantity empties is erased.
  using PriceLevelMap = std::map<std::int64_t, nlib::price_level>;

  void Touch(std::uint32_t instrument_id, std::int64_t event_ns, std::int64_t recv_ns);

  // Links n to its price level, creating the level if absent.
  void LinkOrders(nlib::order* n);

  // Unlinks n from its price level, erasing the level if n was its last order.
  void UnlinkOrders(nlib::order* n);

  void Erase(nlib::order* n);

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
