#include <cstdint>
#include <limits>
#include <span>

#include <nq/feature.hpp>

namespace nq_example {

struct Candle : nq::FeatureBase {
    static constexpr auto kColumns = nq::MakeColumns({
        {"close", "price", "close of the last record"},
        {"range", "price", "high minus low of the last record"},
    });

    explicit Candle(const nq::Params& p) : scale_(static_cast<double>(p.PriceScale())) {}

    void OnOhlcv(const nq::Ohlcv& ohlcv) noexcept {
        close_ = static_cast<double>(ohlcv.close) / scale_;
        range_ = static_cast<double>(ohlcv.high - ohlcv.low) / scale_;
    }

    void Read(std::span<double> out) const noexcept {
        out[0] = close_;
        out[1] = range_;
    }

private:
    double scale_;
    double close_ = std::numeric_limits<double>::quiet_NaN();
    double range_ = std::numeric_limits<double>::quiet_NaN();
};

struct Volume : nq::FeatureBase {
    static constexpr auto kColumns = nq::MakeColumns({
        {"volume", "qty", "cumulative volume", nq::kCumulative},
        {"count", "count", "cumulative record count", nq::kCumulative},
    });

    explicit Volume(const nq::Params&) {}

    void OnOhlcv(const nq::Ohlcv& ohlcv) noexcept {
        volume_ += ohlcv.volume;
        ++count_;
    }

    void OnControl(const nq::Control& c) noexcept {
        if (c.type == nq::ControlType::Reset) {
            volume_ = 0.0;
            count_ = 0;
        }
    }

    void Read(std::span<double> out) const noexcept {
        out[0] = volume_;
        out[1] = static_cast<double>(count_);
    }

private:
    double volume_ = 0.0;
    std::int64_t count_ = 0;
};

}  // namespace nq_example

NQ_OHLCV_PLUGIN(nq_example::Candle, nq_example::Volume)
