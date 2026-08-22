#include <Orderbook.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include <nlib/common.h>

namespace nq {

void Orderbook::OnOrder(const nlib::order& o) {
  if (o.action != nlib::order_action::add || o.type != nlib::order_type::limit) return;
  Touch(o.instrument_id, o.event_ns, o.recv_ns);
  if (by_id_.contains(o.order_id)) return;
  nlib::order* n = &*orders_.emplace(o);
  by_id_.try_emplace(std::int64_t{o.order_id}, n);
  Link(n);
}

void Orderbook::OnTrade(const nlib::trade& t) {
  Touch(t.instrument_id, t.event_ns, t.recv_ns);
  Reduce(t.buy_order_id, t.qty);
  Reduce(t.sell_order_id, t.qty);
}

void Orderbook::OnCancel(const nlib::order& o) {
  Touch(o.instrument_id, o.event_ns, o.recv_ns);
  Reduce(o.order_id, o.cancel_qty);
}

void Orderbook::OnModify(const nlib::order& o) {
  Touch(o.instrument_id, o.event_ns, o.recv_ns);
  const auto it = by_id_.find(o.order_id);
  if (it == by_id_.end()) return;
  nlib::order* n = it->second;
  if (o.new_qty <= 0) {
    Erase(n);
  } else if (n->price == o.price) {
    n->qty = o.new_qty;
  } else {
    Unlink(n);
    n->price = o.price;
    n->qty = o.new_qty;
    Link(n);
  }
}

void Orderbook::OnClear(const nlib::order& o) {
  Touch(o.instrument_id, o.event_ns, o.recv_ns);
  orders_.clear();
  by_id_.clear();
  bid_head_ = ask_head_ = nullptr;
}

nlib::book Orderbook::OnSnapshot() const {
  nlib::book b{};
  b.event_ns = event_ns_;
  b.recv_ns = recv_ns_;
  b.instrument_id = instrument_id_;
  FillSide(bid_head_, b.bid_price, b.bid_qty);
  FillSide(ask_head_, b.ask_price, b.ask_qty);
  return b;
}

std::size_t Orderbook::MemoryBytes() const {
  return orders_.capacity() * sizeof(nlib::order) +
         by_id_.capacity() * (sizeof(std::pair<std::int64_t, nlib::order*>) + 1);
}

void Orderbook::Touch(std::uint32_t instrument_id, std::int64_t event_ns, std::int64_t recv_ns) {
  instrument_id_ = instrument_id;
  event_ns_ = event_ns;
  recv_ns_ = recv_ns;
}

void Orderbook::Link(nlib::order* n) {
  const bool buy = n->side == nlib::side::buy;
  nlib::order*& head = buy ? bid_head_ : ask_head_;
  nlib::order* prev = nullptr;
  nlib::order* cur = head;
  // Walks past every order of better or equal price priority, so `n` lands
  // behind its own level and ahead of every worse one.
  while (cur && (buy ? cur->price >= n->price : cur->price <= n->price)) {
    prev = cur;
    cur = cur->next;
  }
  n->prev = prev;
  n->next = cur;
  (prev ? prev->next : head) = n;
  if (cur) cur->prev = n;
}

void Orderbook::Unlink(nlib::order* n) {
  nlib::order*& head = (n->side == nlib::side::buy) ? bid_head_ : ask_head_;
  (n->prev ? n->prev->next : head) = n->next;
  if (n->next) n->next->prev = n->prev;
}

void Orderbook::Erase(nlib::order* n) {
  Unlink(n);
  by_id_.erase(by_id_.find(n->order_id));
  orders_.erase(orders_.get_iterator(n));
}

void Orderbook::Reduce(std::int64_t order_id, std::int64_t qty) {
  const auto it = by_id_.find(order_id);
  if (it == by_id_.end()) return;
  nlib::order* n = it->second;
  n->qty -= qty;
  if (n->qty <= 0) Erase(n);
}

void Orderbook::FillSide(const nlib::order* n, std::int64_t* price, std::int64_t* qty) {
  // The list is already price-ordered, so one level is the run of orders
  // sharing the head price; the inner loop sums it and leaves `n` on the next.
  for (std::size_t lvl = 0; n && lvl < nlib::book_depth; ++lvl) {
    price[lvl] = n->price;
    for (; n && n->price == price[lvl]; n = n->next) qty[lvl] += n->qty;
  }
}

}  // namespace nq
