#include <Orderbook.h>

#include <cstddef>
#include <cstdint>

#include <nlib/common.h>

namespace nq {

void Orderbook::OnOrder(const nlib::order& o) {
  if (o.action != nlib::order_action::add || o.type != nlib::order_type::limit) return;
  Touch(o.instrument_id, o.time_ns);
  nlib::order* n = &*orders_.emplace(o);
  by_id_.try_emplace(std::int64_t{o.order_id}, n);
  Link(n);
}

void Orderbook::OnTrade(const nlib::trade& t) {
  Touch(t.instrument_id, t.time_ns);
  Reduce(t.buy_order_id, t.qty);
  Reduce(t.sell_order_id, t.qty);
}

void Orderbook::OnCancel(const nlib::order& o) {
  Touch(o.instrument_id, o.time_ns);
  Reduce(o.order_id, o.qty);
}

nlib::book Orderbook::OnSnapshot() const {
  nlib::book b{};
  b.time_ns = time_ns_;
  b.instrument_id = instrument_id_;
  FillSide(bid_head_, b.bid_price, b.bid_qty);
  FillSide(ask_head_, b.ask_price, b.ask_qty);
  return b;
}

void Orderbook::Touch(std::uint32_t instrument_id, std::int64_t time_ns) {
  instrument_id_ = instrument_id;
  time_ns_ = time_ns;
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

void Orderbook::Reduce(std::int64_t order_id, std::int64_t qty) {
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

void Orderbook::FillSide(const nlib::order* n, std::int64_t* price, std::int64_t* qty) {
  // The list is already price-ordered, so one level is the run of orders
  // sharing the head price; the inner loop sums it and leaves `n` on the next.
  for (std::size_t lvl = 0; n && lvl < nlib::book_depth; ++lvl) {
    price[lvl] = n->price;
    for (; n && n->price == price[lvl]; n = n->next) qty[lvl] += n->qty;
  }
}

}  // namespace nq
