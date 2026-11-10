#pragma once

#include <cstddef>

#include <nlib/common.h>

#include <Defaults.h>
#include <Feature/FeatureEngine.h>
#include <Feature/FeatureCatalog.h>
#include <Orderbook.h>

namespace nq {

class Pipeline {
public:
    Pipeline(const FeatureCatalog& catalog, Instrument spec, std::size_t expected_orders = defaults::kExpectedOrders,
             RowSink* sink = nullptr)
        : book_(expected_orders), engine_(book_, catalog, std::move(spec), sink) {}

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Orderbook& Book() noexcept { return book_; }
    const Orderbook& Book() const noexcept { return book_; }
    FeatureEngine& Engine() noexcept { return engine_; }
    const FeatureEngine& Engine() const noexcept { return engine_; }

    nlib::qty_t Apply(const nq_mbo& m) {
        const nlib::qty_t filled = book_.Apply(m);
        engine_.OnEvent(m, filled);
        return filled;
    }

    void Close() { engine_.Close(); }

private:
    Orderbook book_;
    FeatureEngine engine_;
};

}  // namespace nq
