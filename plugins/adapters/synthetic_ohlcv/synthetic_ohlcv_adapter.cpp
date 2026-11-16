#include <cstdint>
#include <string>
#include <vector>

#include <nq/adapter.hpp>

namespace nq_synthetic {

class SyntheticOhlcv : public nq::AdapterBase {
public:
    static constexpr const char* kName = "synthetic_ohlcv";
    static constexpr std::int64_t kMinuteNs = 60'000'000'000;

    explicit SyntheticOhlcv(const nq::Params& p)
        : count_(p.RequireInt("count")),
          instruments_(p.GetInt("instruments", 1)),
          period_ns_(p.GetInt("period_ns", kMinuteNs)),
          price_scale_(p.PriceScale()) {
        for (std::int64_t i = 0; i < instruments_; ++i) {
            table_.push_back(nq::Instrument{static_cast<std::uint32_t>(i + 1), "SYN" + std::to_string(i + 1), 10, 1});
        }
    }

    nq::Next Next(nq::Ohlcv& out) {
        if (next_ >= count_) return nq::Next::End;
        const std::int64_t i = next_++;
        const std::int64_t period_index = i / instruments_;
        const std::int64_t instrument_index = i % instruments_;
        out.ts = period_index * period_ns_;
        out.open = (100 + period_index) * price_scale_;
        out.high = out.open + 2 * price_scale_;
        out.low = out.open - price_scale_;
        out.close = out.open + price_scale_;
        out.volume = 1.0 + static_cast<double>(instrument_index);
        out.instrument = static_cast<std::uint32_t>(instrument_index + 1);
        return nq::Next::Event;
    }

    const nq::Instrument* FindInstrument(std::uint32_t id) const noexcept {
        for (const auto& i : table_) {
            if (i.id == id) return &i;
        }
        return nullptr;
    }

private:
    std::int64_t count_;
    std::int64_t instruments_;
    std::int64_t period_ns_;
    std::int64_t price_scale_;
    std::int64_t next_ = 0;
    std::vector<nq::Instrument> table_;
};

}  // namespace nq_synthetic

NQ_OHLCV_ADAPTER(nq_synthetic::SyntheticOhlcv)
