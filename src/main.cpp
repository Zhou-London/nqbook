// Smoke test: replays a small feed through nq::Orderbook and prints the book
// after each phase for eyeball comparison against the expected lines.
#include <cstdint>
#include <cstdio>

#include <Orderbook.h>
#include <nlib/common.h>

namespace {

std::int64_t now = 0;  // fake feed clock, one tick per event

nlib::order Add(std::int64_t id, nlib::side side, std::int64_t price, std::int64_t qty) {
  return {.seq = now,
          .order_id = id,
          .price = price,
          .qty = qty,
          .time_ns = ++now,
          .instrument_id = 1,
          .side = side,
          .type = nlib::order_type::limit,
          .action = nlib::order_action::add};
}

nlib::order Cancel(std::int64_t id, nlib::side side, std::int64_t qty) {
  nlib::order o = Add(id, side, 0, qty);
  o.action = nlib::order_action::cancel;
  return o;
}

nlib::trade Fill(std::int64_t buy_id, std::int64_t sell_id, std::int64_t price,
                 std::int64_t qty) {
  return {.seq = now,
          .buy_order_id = buy_id,
          .sell_order_id = sell_id,
          .price = price,
          .qty = qty,
          .time_ns = ++now,
          .instrument_id = 1,
          .side = nlib::side::buy};
}

void Print(const char* tag, const nlib::book& b) {
  std::printf("-- %s --\n", tag);
  for (std::size_t i = nlib::book_depth; i-- > 0;)
    if (b.ask_qty[i] != 0)
      std::printf("  ask[%zu] %6lld x %lld\n", i, static_cast<long long>(b.ask_price[i]),
                  static_cast<long long>(b.ask_qty[i]));
  for (std::size_t i = 0; i < nlib::book_depth; ++i)
    if (b.bid_qty[i] != 0)
      std::printf("  bid[%zu] %6lld x %lld\n", i, static_cast<long long>(b.bid_price[i]),
                  static_cast<long long>(b.bid_qty[i]));
}

}  // namespace

int main() {
  nq::Orderbook book;

  book.OnOrder(Add(1, nlib::side::buy, 100100, 300));
  book.OnOrder(Add(2, nlib::side::buy, 100000, 500));
  book.OnOrder(Add(3, nlib::side::buy, 100100, 200));  // joins order 1's level
  book.OnOrder(Add(4, nlib::side::sell, 100300, 400));
  book.OnOrder(Add(5, nlib::side::sell, 100200, 100));
  book.OnOrder(Add(6, nlib::side::sell, 100300, 600));
  Print("after adds (expect asks 100300x1000, 100200x100; bids 100100x500, 100000x500)",
        book.OnSnapshot());

  book.OnOrder(Add(7, nlib::side::buy, 100200, 150));  // aggressor rests, then trades
  book.OnTrade(Fill(7, 5, 100200, 100));               // order 5 fully filled, 7 partial
  Print("after trade (expect ask 100200 gone; bid 100200x50 on top)", book.OnSnapshot());

  book.OnCancel(Cancel(6, nlib::side::sell, 600));  // full cancel
  book.OnCancel(Cancel(2, nlib::side::buy, 400));   // partial cancel, 100 remains
  Print("after cancels (expect ask 100300x400; bid 100000x100 at level 2)",
        book.OnSnapshot());

  book.OnTrade(Fill(7, 4, 100200, 50));  // clears the resting remainder of 7
  book.OnCancel(Cancel(99, nlib::side::sell, 10));  // unknown id, no effect
  Print("final (expect asks 100300x350; bids 100100x500, 100000x100)", book.OnSnapshot());
  return 0;
}
