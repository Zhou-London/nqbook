#include <Feature/BookAdapter.h>

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

BookAdapter::BookAdapter(const Orderbook& book) noexcept
    : book_(&book), view_{this, &BookAdapter::Best, &BookAdapter::Level, &BookAdapter::Ts} {}

int BookAdapter::Best(const void* ctx, std::uint8_t side, nq_level* out) noexcept {
    const Orderbook& book = *static_cast<const BookAdapter*>(ctx)->book_;
    const nlib::level* lv = side == NQ_BID ? book.BestBid() : book.BestAsk();
    if (!lv) return 1;
    *out = ToLevel(*lv);
    return 0;
}

int BookAdapter::Level(const void* ctx, std::uint8_t side, std::uint32_t k, nq_level* out) noexcept {
    const Orderbook& book = *static_cast<const BookAdapter*>(ctx)->book_;
    return side == NQ_BID ? NthLevel(book.Bids(), k, out) : NthLevel(book.Asks(), k, out);
}

std::int64_t BookAdapter::Ts(const void* ctx) noexcept {
    return static_cast<const BookAdapter*>(ctx)->book_->LastTs();
}

}  // namespace nq
