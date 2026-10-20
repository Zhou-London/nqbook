#include <Feature/OrderbookView.h>

namespace nq {

namespace {

nq_level ToLevel(const nlib::level& lv) noexcept { return nq_level{lv.price, lv.qty, lv.count}; }

template <typename Levels>
int NthLevel(const Levels& levels, std::uint32_t k, nq_level* out) noexcept {
    if (k >= levels.size()) return 1;
    auto it = levels.begin();
    for (; k > 0; --k) ++it;
    *out = ToLevel(it->second);
    return 0;
}

}  // namespace

OrderbookView::OrderbookView(const Orderbook& book) noexcept
    : book_(&book), view_{this, &OrderbookView::Best, &OrderbookView::Level, &OrderbookView::Ts} {}

int OrderbookView::Best(const void* ctx, std::uint8_t side, nq_level* out) noexcept {
    const Orderbook& book = *static_cast<const OrderbookView*>(ctx)->book_;
    const nlib::level* lv = side == NQ_BID ? book.BestBid() : book.BestAsk();
    if (!lv) return 1;
    *out = ToLevel(*lv);
    return 0;
}

int OrderbookView::Level(const void* ctx, std::uint8_t side, std::uint32_t k, nq_level* out) noexcept {
    const Orderbook& book = *static_cast<const OrderbookView*>(ctx)->book_;
    return side == NQ_BID ? NthLevel(book.Bids(), k, out) : NthLevel(book.Asks(), k, out);
}

std::int64_t OrderbookView::Ts(const void* ctx) noexcept {
    return static_cast<const OrderbookView*>(ctx)->book_->LastTs();
}

}  // namespace nq
