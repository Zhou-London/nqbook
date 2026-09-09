#include <Orderbook.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include <nlib/common.h>

namespace nq {
void Orderbook::OnOrder(const nlib::order &o) {
  Touch(o.instrument_id, o.event_ns, o.recv_ns);
  switch (o.action) {
  case nlib::order_action::add:
    return Add(o);
  case nlib::order_action::modify:
    return Modify(o);
  }
}

void Orderbook::OnTrade(const nlib::trade &t) {
  Touch(t.instrument_id, t.event_ns, t.recv_ns);
  Reduce(t.buy_order_id, t.qty);
  Reduce(t.sell_order_id, t.qty);
}

void Orderbook::OnCancel(const nlib::cancel &c) {
  Touch(c.instrument_id, c.event_ns, c.recv_ns);
  Reduce(c.order_id, c.qty);
}

void Orderbook::OnLevel(const nlib::level &l) {
  Touch(l.instrument_id, l.event_ns, l.recv_ns);
  PriceLevelMap &side = (l.side == nlib::side::buy) ? bids_ : asks_;
  if (l.qty <= 0) {
    side.erase(l.price);
    return;
  }
  // Aggregate level: qty is the absolute total, the order queue stays empty.
  side.try_emplace(l.price).first->second.qty = l.qty;
}

nlib::book Orderbook::OnSnapshot() const {
  nlib::book b{};
  b.event_ns = event_ns_;
  b.recv_ns = recv_ns_;
  b.instrument_id = instrument_id_;

  // bids start from reverse
  std::size_t level_idx = 0;
  for (auto it = bids_.rbegin();
       it != bids_.rend() && level_idx < nlib::snapshot_depth;
       ++it, ++level_idx) {
    b.bid_price[level_idx] = it->first;
    b.bid_qty[level_idx] = it->second.qty;
  }

  level_idx = 0;
  for (auto it = asks_.begin();
       it != asks_.end() && level_idx < nlib::snapshot_depth;
       ++it, ++level_idx) {
    b.ask_price[level_idx] = it->first;
    b.ask_qty[level_idx] = it->second.qty;
  }
  return b;
}

std::size_t Orderbook::MemoryBytes() const {
  constexpr std::size_t kLevelNode =
      sizeof(std::pair<const std::int64_t, nlib::price_level>) +
      4 * sizeof(void *);
  return orders_.capacity() * sizeof(nlib::order) +
         by_id_.capacity() *
             (sizeof(std::pair<std::int64_t, nlib::order *>) + 1) +
         (bids_.size() + asks_.size()) * kLevelNode;
}

void Orderbook::Add(const nlib::order &o) {
  if (o.type != nlib::order_type::limit)
    return;
  if (by_id_.contains(o.order_id))
    return;

  nlib::order *pOrder =
      &*orders_.emplace(o); // Emplaces the order and get its pointer
  by_id_.try_emplace(std::int64_t{o.order_id}, pOrder);

  LinkOrders(pOrder);
}

void Orderbook::Modify(const nlib::order &o) {
  const auto it = by_id_.find(o.order_id);
  if (it == by_id_.end())
    return;

  nlib::order *pOrder = it->second;
  if (o.new_qty <= 0) {
    Erase(pOrder);
  } else if (pOrder->price == o.price) {
    FindLevel(pOrder).qty += o.new_qty - pOrder->qty;
    pOrder->qty = o.new_qty;
  } else {
    // Takes off the order, applies changes, puts it back
    UnlinkOrders(pOrder);
    pOrder->price = o.price;
    pOrder->qty = o.new_qty;
    LinkOrders(pOrder);
  }
}

void Orderbook::Touch(std::uint32_t instrument_id, std::int64_t event_ns,
                      std::int64_t recv_ns) {
  instrument_id_ = instrument_id;
  event_ns_ = event_ns;
  recv_ns_ = recv_ns;
}

void Orderbook::LinkOrders(nlib::order *n) {
  // Finds price level, creates if not exist
  PriceLevelMap &side = n->side == nlib::side::buy ? bids_ : asks_;
  nlib::price_level &lvl = side.try_emplace(n->price).first->second;

  n->prev = lvl.tail;
  n->next = nullptr;
  (lvl.tail ? lvl.tail->next : lvl.head) = n;
  lvl.tail = n;
  lvl.qty += n->qty;
}

void Orderbook::UnlinkOrders(nlib::order *n) {
  // Finds price level
  PriceLevelMap &side = n->side == nlib::side::buy ? bids_ : asks_;
  const auto it = side.find(n->price);
  nlib::price_level &lvl = it->second;

  (n->prev ? n->prev->next : lvl.head) = n->next;
  (n->next ? n->next->prev : lvl.tail) = n->prev;
  lvl.qty -= n->qty;
  if (lvl.head == nullptr)
    side.erase(it);
}

void Orderbook::Erase(nlib::order *n) {
  UnlinkOrders(n);
  by_id_.erase(by_id_.find(n->order_id));
  orders_.erase(orders_.get_iterator(n));
}

void Orderbook::Reduce(std::int64_t order_id, std::int64_t qty) {
  // Finds order's pointer
  const auto it = by_id_.find(order_id);
  if (it == by_id_.end())
    return;

  nlib::order *pOrder = it->second;
  if (pOrder->qty <= qty) {
    Erase(pOrder);
  } else {
    pOrder->qty -= qty;
    FindLevel(pOrder).qty -= qty;
  }
}

nlib::price_level &Orderbook::FindLevel(nlib::order *n) {
  PriceLevelMap &level_map = n->side == nlib::side::buy ? bids_ : asks_;
  return level_map.find(n->price)->second;
}

} // namespace nq
