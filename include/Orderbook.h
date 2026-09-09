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
  void OnOrder(const nlib::order &o);
  void OnTrade(const nlib::trade &t);
  void OnCancel(const nlib::cancel &c);
  void OnLevel(const nlib::level &l);
  nlib::book OnSnapshot() const;

  std::size_t size() const { return by_id_.size(); }
  std::size_t
  MemoryBytes() const; // Appromixates the memory cost of orderbook instance
private:
  // ---------- Helper functions ----------
  // Rests a limit order; drops repeated ids.
  void Add(const nlib::order &o);

  // Sets an order's remaining quantity, requeueing it on a price change.
  void Modify(const nlib::order &o);

  // Stamps later snapshots with instrument_id and the two event times.
  void Touch(std::uint32_t instrument_id, std::int64_t event_ns,
             std::int64_t recv_ns);

  // Links n to its price level, creating the level if absent.
  void LinkOrders(nlib::order *n);

  // Unlinks order from its price level, erasing the level if order was the
  // last.
  void UnlinkOrders(nlib::order *n);

  // Unlinks order, frees its storage and its id entry.
  void Erase(nlib::order *n);

  // Shrinks order by qty
  void Reduce(std::int64_t order_id, std::int64_t qty);

  // Returns the corresponding level
  nlib::price_level &FindLevel(nlib::order *n);

private:
  // ----------- Member Variables ----------
  using PriceLevelMap = std::map<std::int64_t, nlib::price_level>;

  nlib::hive<nlib::order> orders_;
  nlib::map<std::int64_t, nlib::order *> by_id_;
  PriceLevelMap bids_;
  PriceLevelMap asks_;
  std::int64_t event_ns_ = 0;
  std::int64_t recv_ns_ = 0;
  std::uint32_t instrument_id_ = 0;
};

} // namespace nq
