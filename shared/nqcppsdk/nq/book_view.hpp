#pragma once

#include <cstdint>
#include <optional>

#include <nq/feature_abi.h>
#include <nq/types.hpp>

namespace nq {

using Level = nq_level;

class BookView {
public:
    explicit BookView(const nq_book_view* view) noexcept : view_(view) {}

    std::optional<Level> Best(Side side) const noexcept {
        Level out;
        if (view_->best(view_->ctx, static_cast<std::uint8_t>(side), &out) != 0) return std::nullopt;
        return out;
    }

    std::optional<Level> GetLevel(Side side, std::uint32_t k) const noexcept {
        Level out;
        if (view_->level(view_->ctx, static_cast<std::uint8_t>(side), k, &out) != 0) return std::nullopt;
        return out;
    }

    std::int64_t Ts() const noexcept { return view_->ts(view_->ctx); }

    const nq_book_view* Raw() const noexcept { return view_; }

private:
    const nq_book_view* view_;
};

}  // namespace nq
