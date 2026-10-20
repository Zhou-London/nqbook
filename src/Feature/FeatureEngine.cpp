#include <Feature/FeatureEngine.h>

#include <nq/params.hpp>

namespace nq {

FeatureEngine::FeatureEngine(const Orderbook& book, const FeatureCatalog& catalog, Instrument spec)
    : view_(book), catalog_(&catalog), spec_(std::move(spec)) {
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
    for (const auto& t : catalog.Samplers()) {
        samplers_.push_back(std::make_unique<Sampler>(t.spec, t.names, t.columns));
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
            instances_[i].Read(all.subspan(offsets_[i], instances_[i].Ncol()), view_.View());
        }
        values_fresh_ = true;
    }
    return values_;
}

void FeatureEngine::Advance(std::int64_t ts) {
    for (auto& s : samplers_) s->Advance(ts, [this] { return ReadAll(); });
}

void FeatureEngine::Close() {
    for (auto& s : samplers_) s->Close([this] { return ReadAll(); });
}

void FeatureEngine::OnEvent(const nq_mbo& m, nlib::qty_t filled) {
    values_fresh_ = false;
    if (m.action == NQ_CLEAR) {
        const nq_control reset{NQ_CONTROL_RESET, m.ts};
        for (auto& inst : instances_) inst.OnControl(reset);
        for (auto& s : samplers_) s->ResetBaseline();
    } else {
        const nq_event e{m.ts, m.id, m.price, m.qty, filled,
                         static_cast<std::uint8_t>(m.side), static_cast<std::uint8_t>(m.action)};
        for (auto& inst : instances_) inst.OnEvent(e, view_.View());
    }
    for (auto& s : samplers_) {
        if (s->OnEvent(m.ts, m.action)) s->Emit(m.ts, ReadAll());
    }
}

}  // namespace nq
