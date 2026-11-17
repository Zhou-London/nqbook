#pragma once

#include <cstddef>
#include <utility>

#include <nq/abi.h>

#include <Defaults.h>
#include <Feature/FeatureCatalog.h>
#include <Feature/FeatureEngine.h>
#include <Feature/OrderbookView.h>
#include <Orderbook.h>

namespace nq {

template <class Record>
struct PipelineOptions {};

template <>
struct PipelineOptions<nq_mbo> {
    std::size_t expected_orders = defaults::kExpectedOrders;
};

template <class Record>
class Pipeline;

template <>
class Pipeline<nq_mbo> {
public:
    Pipeline(const FeatureCatalog& catalog, Instrument spec, PipelineOptions<nq_mbo> options = {},
             RowSink* sink = nullptr)
        : book_(options.expected_orders), view_(MakeBookView(book_)), engine_(catalog, std::move(spec), sink, &view_) {}

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    const Orderbook& Book() const noexcept { return book_; }
    FeatureEngine& Engine() noexcept { return engine_; }
    const FeatureEngine& Engine() const noexcept { return engine_; }

    void Apply(const nq_mbo& m) { engine_.OnEvent(m, book_.Apply(m)); }

    void Close() { engine_.Close(); }

private:
    Orderbook book_;
    nq_book_view view_;
    FeatureEngine engine_;
};

template <>
class Pipeline<nq_ohlcv> {
public:
    Pipeline(const FeatureCatalog& catalog, Instrument spec, PipelineOptions<nq_ohlcv> = {}, RowSink* sink = nullptr)
        : engine_(catalog, std::move(spec), sink) {}

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    FeatureEngine& Engine() noexcept { return engine_; }
    const FeatureEngine& Engine() const noexcept { return engine_; }

    void Apply(const nq_ohlcv& ohlcv) { engine_.OnOhlcv(ohlcv); }

    void Close() { engine_.Close(); }

private:
    FeatureEngine engine_;
};

}  // namespace nq
