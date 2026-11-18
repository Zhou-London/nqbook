#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <nq/adapter.hpp>

namespace nq_synthetic {

class Synthetic : public nq::AdapterBase {
public:
    static constexpr const char* kName = "synthetic";
    static constexpr nq::Provides kProvides = nq::Provides::MBO;
    static constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::max();

    explicit Synthetic(const nq::Params& p)
        : end_after_(p.GetBool("endless", false) ? kNever : p.RequireInt("count")),
          instruments_(p.GetInt("instruments", 1)),
          again_every_(p.GetInt("again_every", 0)),
          fail_after_(p.GetInt("fail_after", kNever)),
          warmup_bars_(p.GetInt("warmup", 0)),
          price_scale_(p.PriceScale()) {
        for (std::int64_t i = 0; i < instruments_; ++i) {
            table_.push_back(nq::Instrument{static_cast<std::uint32_t>(i + 1), "SYN" + std::to_string(i + 1), 10, 1});
        }
    }

    nq::Next Next(nq::Mbo& m) {
        if (next_ >= fail_after_) return nq::Next::Error;
        if (next_ >= end_after_) return nq::Next::End;
        if (again_every_ > 0 && ++calls_ % again_every_ == 0) return nq::Next::Again;
        const std::int64_t i = next_++;
        m.ts = i + 1;
        m.id = static_cast<std::uint64_t>(i + 1);
        m.price = 1000 + i;
        m.qty = 1;
        m.instrument = static_cast<std::uint32_t>(i % instruments_ + 1);
        m.side = i % 2 ? nq::Side::Ask : nq::Side::Bid;
        m.action = nq::Action::Add;
        return nq::Next::Event;
    }

    nq::Next Warmup(nq::Ohlcv& out) {
        if (warmed_ >= warmup_bars_) return nq::Next::End;
        const std::int64_t i = warmed_++;
        out.ts = i - warmup_bars_;
        out.open = (100 + i) * price_scale_;
        out.high = out.open + 2 * price_scale_;
        out.low = out.open - price_scale_;
        out.close = out.open + price_scale_;
        out.volume = 1.0;
        out.instrument = static_cast<std::uint32_t>(i % instruments_ + 1);
        return nq::Next::Event;
    }

    const nq::Instrument* FindInstrument(std::uint32_t id) const noexcept {
        for (const auto& i : table_) {
            if (i.id == id) return &i;
        }
        return nullptr;
    }

private:
    std::int64_t end_after_;
    std::int64_t instruments_;
    std::int64_t again_every_;
    std::int64_t fail_after_;
    std::int64_t warmup_bars_;
    std::int64_t price_scale_;
    std::int64_t warmed_ = 0;
    std::int64_t next_ = 0;
    std::int64_t calls_ = 0;
    std::vector<nq::Instrument> table_;
};

}  // namespace nq_synthetic

NQ_ADAPTER(nq_synthetic::Synthetic)
