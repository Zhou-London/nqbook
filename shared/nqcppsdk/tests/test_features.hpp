#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

#include <nq/feature.hpp>

namespace nq_test {

struct Spread : nq::FeatureBase {
    static constexpr auto kColumns = nq::MakeColumns({
        {"spread", "price", "best ask minus best bid"},
        {"mid", "price", "midpoint of best bid and ask"},
    });
    static constexpr nq::Requires kRequires = nq::Requires::L1;

    explicit Spread(const nq::Params& p) : scale_(static_cast<double>(p.PriceScale())) {}

    void Read(std::span<double> out, const nq::BookView& book) const noexcept {
        const auto bid = book.Best(nq::Side::Bid);
        const auto ask = book.Best(nq::Side::Ask);
        if (!bid || !ask) {
            out[0] = std::numeric_limits<double>::quiet_NaN();
            out[1] = std::numeric_limits<double>::quiet_NaN();
            return;
        }
        out[0] = static_cast<double>(ask->price - bid->price) / scale_;
        out[1] = static_cast<double>(ask->price + bid->price) / (2.0 * scale_);
    }

private:
    double scale_;
};

struct TradeVolume : nq::FeatureBase {
    static constexpr auto kColumns = nq::MakeColumns({
        {"buy_vol", "qty", "cumulative aggressor-buy volume", nq::kCumulative},
        {"sell_vol", "qty", "cumulative aggressor-sell volume", nq::kCumulative},
        {"trades", "count", "cumulative trade count", nq::kCumulative},
    });

    explicit TradeVolume(const nq::Params& p)
        : scale_(static_cast<double>(p.QtyScale())), min_qty_(p.GetInt("min_qty", 0)) {}

    void OnEvent(const nq::Event& e, const nq::BookView&) noexcept {
        if (e.action != nq::Action::Trade || e.filled < min_qty_) return;
        if (e.side == nq::Side::Ask) buy_ += e.filled;
        else sell_ += e.filled;
        ++trades_;
    }

    void OnControl(const nq::Control& c) noexcept {
        if (c.type == nq::ControlType::Reset) {
            buy_ = 0;
            sell_ = 0;
            trades_ = 0;
        }
    }

    void Read(std::span<double> out, const nq::BookView&) const noexcept {
        out[0] = static_cast<double>(buy_) / scale_;
        out[1] = static_cast<double>(sell_) / scale_;
        out[2] = static_cast<double>(trades_);
    }

private:
    double scale_;
    std::int64_t min_qty_;
    std::int64_t buy_ = 0;
    std::int64_t sell_ = 0;
    std::int64_t trades_ = 0;
};

}  // namespace nq_test
