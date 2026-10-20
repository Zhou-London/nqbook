#pragma once

#include <cstdint>

#include <nq/feature_abi.h>

#include <Orderbook.h>

namespace nq {

class OrderbookView {
public:
    explicit OrderbookView(const Orderbook& book) noexcept;

    OrderbookView(const OrderbookView&) = delete;
    OrderbookView& operator=(const OrderbookView&) = delete;

    const nq_book_view& View() const noexcept { return view_; }
    const Orderbook& Book() const noexcept { return *book_; }

private:
    static int Best(const void* ctx, std::uint8_t side, nq_level* out) noexcept;
    static int Level(const void* ctx, std::uint8_t side, std::uint32_t k, nq_level* out) noexcept;
    static std::int64_t Ts(const void* ctx) noexcept;

    const Orderbook* book_;
    nq_book_view view_;
};

}  // namespace nq
