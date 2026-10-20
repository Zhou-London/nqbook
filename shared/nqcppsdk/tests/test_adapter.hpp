#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nq/adapter.hpp>

namespace nq_test {

class Synthetic : public nq::AdapterBase {
public:
    static constexpr const char* kName = "synthetic";
    static constexpr nq::Provides kProvides = nq::Provides::MBO;

    explicit Synthetic(const nq::Params& p)
        : count_(p.RequireInt("count")),
          instruments_(p.GetInt("instruments", 1)),
          endless_(p.GetBool("endless", false)),
          again_every_(p.GetInt("again_every", 0)) {
        for (std::int64_t i = 0; i < instruments_; ++i) {
            table_.push_back(nq::Instrument{static_cast<std::uint32_t>(i + 1), "SYN" + std::to_string(i + 1), 10, 1});
        }
    }

    nq::Next Next(nq::Mbo& m) {
        if (!endless_ && next_ >= count_) return nq::Next::End;
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

    const nq::Instrument* FindInstrument(std::uint32_t id) const noexcept {
        for (const auto& i : table_) {
            if (i.id == id) return &i;
        }
        return nullptr;
    }

private:
    std::int64_t count_;
    std::int64_t instruments_;
    bool endless_;
    std::int64_t again_every_;
    std::int64_t next_ = 0;
    std::int64_t calls_ = 0;
    std::vector<nq::Instrument> table_;
};

}  // namespace nq_test
