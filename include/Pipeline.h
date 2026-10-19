#pragma once

#include <cstddef>
#include <limits>

#include <nlib/common.h>

#include <Feature/FeatureEngine.h>
#include <Orderbook.h>

namespace nq {

class Pipeline {
public:
    explicit Pipeline(InstrumentSpec spec, std::size_t expected_orders = 1 << 16)
        : book_(expected_orders), engine_(book_, std::move(spec)) {}

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Orderbook& Book() noexcept { return book_; }
    const Orderbook& Book() const noexcept { return book_; }
    FeatureEngine& Engine() noexcept { return engine_; }
    const FeatureEngine& Engine() const noexcept { return engine_; }

    nlib::qty_t Apply(const nlib::mbo& m) {
        engine_.Advance(m.ts);
        const nlib::qty_t filled = book_.Apply(m);
        engine_.OnEvent(m, filled);
        return filled;
    }

    std::size_t Consume(Orderbook::Queue& q, std::size_t max = std::numeric_limits<std::size_t>::max()) {
        std::size_t n = 0;
        while (n < max) {
            auto m = q.try_pop();
            if (!m) break;
            Apply(*m);
            ++n;
        }
        return n;
    }

private:
    Orderbook book_;
    FeatureEngine engine_;
};

}  // namespace nq
