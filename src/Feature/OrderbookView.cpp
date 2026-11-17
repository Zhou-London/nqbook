#include <Feature/OrderbookView.h>

#include <cstdint>

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

const Orderbook& BookOf(const void* ctx) noexcept { return *static_cast<const Orderbook*>(ctx); }

int Best(const void* ctx, std::uint8_t side, nq_level* out) noexcept {
    const nlib::level* lv = side == NQ_BID ? BookOf(ctx).BestBid() : BookOf(ctx).BestAsk();
    if (!lv) return 1;
    *out = ToLevel(*lv);
    return 0;
}

int Level(const void* ctx, std::uint8_t side, std::uint32_t k, nq_level* out) noexcept {
    return side == NQ_BID ? NthLevel(BookOf(ctx).Bids(), k, out) : NthLevel(BookOf(ctx).Asks(), k, out);
}

std::int64_t Ts(const void* ctx) noexcept { return BookOf(ctx).LastTs(); }

}  // namespace

nq_book_view MakeBookView(const Orderbook& book) noexcept { return {&book, &Best, &Level, &Ts}; }

}  // namespace nq
