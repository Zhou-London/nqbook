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
// the feed reports their trades.
class Orderbook {
 public:
  // Rests a limit-order add on its side of the book. Anything else is
  // dropped: market orders cannot rest without matching, and cancels arrive
  // through OnCancel().
  void OnOrder(const nlib::order& o) {
    if (o.action != nlib::order_action::add || o.type != nlib::order_type::limit) return;
    Touch(o.instrument_id, o.time_ns);
    nlib::order* n = &*orders_.emplace(o);
    by_id_.try_emplace(std::int64_t{o.order_id}, n);
    Link(n);
  }

  // Applies an execution: both resting sides shrink by t.qty and leave the
  // book once fully filled. Ids that never rested are skipped.
  void OnTrade(const nlib::trade& t) {
    Touch(t.instrument_id, t.time_ns);
    Reduce(t.buy_order_id, t.qty);
    Reduce(t.sell_order_id, t.qty);
  }

  // Applies a cancel: order o.order_id shrinks by the cancelled quantity and
  // leaves the book at zero remaining.
  void OnCancel(const nlib::order& o) {
    Touch(o.instrument_id, o.time_ns);
    Reduce(o.order_id, o.qty);
  }

  // Aggregates the top nlib::book_depth price levels per side, stamped with
  // the latest event time seen.
  nlib::book OnSnapshot() const {
    nlib::book b{};
    b.time_ns = time_ns_;
    b.instrument_id = instrument_id_;
    FillSide(bid_head_, b.bid_price, b.bid_qty);
    FillSide(ask_head_, b.ask_price, b.ask_qty);
    return b;
  }

 private:
  void Touch(std::uint32_t instrument_id, std::int64_t time_ns) {
    instrument_id_ = instrument_id;
    time_ns_ = time_ns;
  }

  // Links `n` behind every order of better or equal price priority, giving
  // price order across levels and arrival order within one.
  void Link(nlib::order* n) {
    const bool buy = n->side == nlib::side::buy;
    nlib::order*& head = buy ? bid_head_ : ask_head_;
    nlib::order* prev = nullptr;
    nlib::order* cur = head;
    while (cur && (buy ? cur->price >= n->price : cur->price <= n->price)) {
      prev = cur;
      cur = cur->next;
    }
    n->prev = prev;
    n->next = cur;
    (prev ? prev->next : head) = n;
    if (cur) cur->prev = n;
  }

  // Shrinks order `order_id` by `qty`; at zero remaining, unlinks it and
  // frees its storage and id entry. Unknown ids are ignored.
  void Reduce(std::int64_t order_id, std::int64_t qty) {
    const auto it = by_id_.find(order_id);
    if (it == by_id_.end()) return;
    nlib::order* n = it->second;
    n->qty -= qty;
    if (n->qty > 0) return;
    nlib::order*& head = (n->side == nlib::side::buy) ? bid_head_ : ask_head_;
    (n->prev ? n->prev->next : head) = n->next;
    if (n->next) n->next->prev = n->prev;
    by_id_.erase(it);
    orders_.erase(orders_.get_iterator(n));
  }

  // Writes up to nlib::book_depth (price, summed qty) levels from one side's
  // list; trailing levels stay zero.
  static void FillSide(const nlib::order* n, std::int64_t* price, std::int64_t* qty) {
    for (std::size_t lvl = 0; n && lvl < nlib::book_depth; ++lvl) {
      price[lvl] = n->price;
      for (; n && n->price == price[lvl]; n = n->next) qty[lvl] += n->qty;
    }
  }

  nlib::hive<nlib::order> orders_;
  nlib::map<std::int64_t, nlib::order*> by_id_;
  nlib::order* bid_head_ = nullptr;  // highest bid first
  nlib::order* ask_head_ = nullptr;  // lowest ask first
  std::int64_t time_ns_ = 0;         // latest event time
  std::uint32_t instrument_id_ = 0;
};

}  // namespace nq
