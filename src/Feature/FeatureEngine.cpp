#include <Feature/FeatureEngine.h>

#include <algorithm>

#include <nq/params.hpp>

namespace nq {

FeatureEngine::FeatureEngine(const FeatureCatalog& catalog, Instrument spec, RowSink* sink, const nq_book_view* book)
    : book_(book), catalog_(&catalog), spec_(std::move(spec)) {
    instances_.reserve(catalog.PluginCount());
    for (std::size_t i = 0; i < catalog.PluginCount(); ++i) {
        ParamsBuilder pb;
        pb.Symbol(spec_.symbol)
            .TickSize(spec_.tick_size)
            .LotSize(spec_.lot_size)
            .PriceScale(NQ_PRICE_SCALE)
            .QtyScale(NQ_QTY_SCALE);
        for (const auto& [k, v] : catalog.PluginParams(i)) pb.Set(k, v);
        offsets_.push_back(values_.size());
        values_.resize(values_.size() + catalog.Plugin(i).Schema().ncol, 0.0);
        instances_.push_back(catalog.Plugin(i).Create(*pb.Build()));
    }
    if (sink) sink->Register(spec_);
    for (std::size_t i = 0; i < catalog.Samplers().size(); ++i) {
        const auto& t = catalog.Samplers()[i];
        samplers_.push_back(std::make_unique<Sampler>(t.spec, t.names, t.columns, sink,
                                                      static_cast<std::uint32_t>(i), spec_.id));
    }
}

Sampler* FeatureEngine::FindSampler(std::string_view name) noexcept {
    for (auto& s : samplers_) {
        if (s->Spec().name == name) return s.get();
    }
    return nullptr;
}

std::span<const double> FeatureEngine::ReadAll() {
    if (!values_fresh_) {
        std::span<double> all(values_);
        for (std::size_t i = 0; i < instances_.size(); ++i) {
            instances_[i].Read(all.subspan(offsets_[i], instances_[i].Ncol()), book_);
        }
        values_fresh_ = true;
    }
    return values_;
}

void FeatureEngine::Advance(std::int64_t ts) {
    for (auto& s : samplers_) s->Advance(ts, [this] { return ReadAll(); });
}

std::int64_t FeatureEngine::Due() const noexcept {
    std::int64_t due = TimerWheel::kNever;
    for (const auto& s : samplers_) due = std::min(due, s->Due());
    return due;
}

void FeatureEngine::Close() {
    for (auto& s : samplers_) s->Close([this] { return ReadAll(); });
}

void FeatureEngine::ResetPlugins(std::int64_t ts) {
    const std::span<const double> before = ReadAll();
    for (auto& s : samplers_) s->CarryDelta(before);

    const nq_control reset{NQ_CONTROL_RESET, ts};
    for (auto& inst : instances_) inst.OnControl(reset);
    values_fresh_ = false;

    const std::span<const double> after = ReadAll();
    for (auto& s : samplers_) s->ResetBaseline(after);
}

void FeatureEngine::OnEvent(const nq_mbo& m, nlib::qty_t filled) {
    values_fresh_ = false;
    if (m.action == NQ_CLEAR) {
        ResetPlugins(m.ts);
    } else {
        const nq_event e{m.ts, m.id, m.price, m.qty, filled,
                         static_cast<std::uint8_t>(m.side), static_cast<std::uint8_t>(m.action)};
        for (auto& inst : instances_) inst.OnEvent(e, *book_);
    }
    NotifySamplers(m.ts, m.action == NQ_TRADE);
}

void FeatureEngine::OnOhlcv(const nq_ohlcv& ohlcv) {
    values_fresh_ = false;
    for (auto& inst : instances_) inst.OnOhlcv(ohlcv);
    NotifySamplers(ohlcv.ts, false);
}

void FeatureEngine::NotifySamplers(std::int64_t ts, bool is_trade) {
    for (auto& s : samplers_) s->OnEvent(ts, is_trade, [this] { return ReadAll(); });
}

}  // namespace nq
