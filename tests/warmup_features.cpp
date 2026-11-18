#include <cstdint>
#include <limits>
#include <span>

#include <nq/feature.hpp>

namespace nqtest {

struct History : nq::FeatureBase {
    static constexpr auto kColumns = nq::MakeColumns({
        {"bars", "count", "warm-up bars received"},
        {"close", "price", "close of the last warm-up bar"},
        {"volume", "qty", "warm-up volume plus traded quantity", nq::kCumulative},
    });

    explicit History(const nq::Params& p)
        : price_scale_(static_cast<double>(p.PriceScale())), qty_scale_(static_cast<double>(p.QtyScale())) {}

    void OnWarmup(const nq::Ohlcv& ohlcv) noexcept {
        ++bars_;
        close_ = static_cast<double>(ohlcv.close) / price_scale_;
        volume_ += ohlcv.volume;
    }

    void OnEvent(const nq::Event& e, const nq::BookView&) noexcept {
        if (e.action == nq::Action::Trade) volume_ += static_cast<double>(e.filled) / qty_scale_;
    }

    void Read(std::span<double> out, const nq::BookView&) const noexcept {
        out[0] = static_cast<double>(bars_);
        out[1] = close_;
        out[2] = volume_;
    }

private:
    double price_scale_;
    double qty_scale_;
    std::int64_t bars_ = 0;
    double close_ = std::numeric_limits<double>::quiet_NaN();
    double volume_ = 0.0;
};

}  // namespace nqtest

NQ_PLUGIN(nqtest::History)
