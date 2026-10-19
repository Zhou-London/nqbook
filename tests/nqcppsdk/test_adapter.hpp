#pragma once

#include <chrono>
#include <cstdint>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <nq/adapter.hpp>

namespace nq_test {

class Synthetic : public nq::ThreadedAdapter<Synthetic> {
public:
    static constexpr const char* kName = "synthetic";
    static constexpr nq::Provides kProvides = nq::Provides::MBO;

    Synthetic(const nq::Params& p, nq::Sink sink)
        : sink_(sink),
          count_(p.RequireInt("count")),
          instruments_(p.GetInt("instruments", 1)),
          delay_us_(p.GetInt("delay_us", 0)),
          endless_(p.GetBool("endless", false)) {
        for (std::int64_t i = 0; i < instruments_; ++i) {
            table_.push_back(nq::Instrument{static_cast<std::uint32_t>(i + 1), "SYN" + std::to_string(i + 1), 10, 1});
        }
    }

    void Run(std::stop_token st) {
        for (std::int64_t i = 0; endless_ || i < count_; ++i) {
            if (st.stop_requested()) return;
            nq::Mbo m;
            m.ts = i + 1;
            m.id = static_cast<std::uint64_t>(i + 1);
            m.price = 1000 + i;
            m.qty = 1;
            m.instrument = static_cast<std::uint32_t>(i % instruments_ + 1);
            m.side = i % 2 ? nq::Side::Ask : nq::Side::Bid;
            m.action = nq::Action::Add;
            sink_.Push(m);
            if (delay_us_ > 0) std::this_thread::sleep_for(std::chrono::microseconds(delay_us_));
        }
        sink_.End(0);
    }

    const nq::Instrument* FindInstrument(std::uint32_t id) const noexcept {
        for (const auto& i : table_) {
            if (i.id == id) return &i;
        }
        return nullptr;
    }

private:
    nq::Sink sink_;
    std::int64_t count_;
    std::int64_t instruments_;
    std::int64_t delay_us_;
    bool endless_;
    std::vector<nq::Instrument> table_;
};

}  // namespace nq_test
