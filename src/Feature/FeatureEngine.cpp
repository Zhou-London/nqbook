#include <Feature/FeatureEngine.h>

#include <nq/params.hpp>

namespace nq {

static_assert(static_cast<std::uint8_t>(nlib::side::bid) == NQ_BID);
static_assert(static_cast<std::uint8_t>(nlib::side::ask) == NQ_ASK);
static_assert(static_cast<std::uint8_t>(nlib::action::add) == NQ_ADD);
static_assert(static_cast<std::uint8_t>(nlib::action::cancel) == NQ_CANCEL);
static_assert(static_cast<std::uint8_t>(nlib::action::modify) == NQ_MODIFY);
static_assert(static_cast<std::uint8_t>(nlib::action::trade) == NQ_TRADE);
static_assert(static_cast<std::uint8_t>(nlib::action::clear) == NQ_CLEAR);

FeatureEngine::FeatureEngine(const Orderbook& book, const PluginSet& plugins, InstrumentSpec spec)
    : adapter_(book), plugins_(&plugins), spec_(std::move(spec)) {
    instances_.reserve(plugins.PluginCount());
    for (std::size_t i = 0; i < plugins.PluginCount(); ++i) {
        ParamsBuilder pb;
        pb.Symbol(spec_.symbol)
            .TickSize(spec_.tick_size)
            .LotSize(spec_.lot_size)
            .PriceScale(nlib::price_scale)
            .QtyScale(nlib::qty_scale);
        for (const auto& [k, v] : plugins.PluginParams(i)) pb.Set(k, v);
        offsets_.push_back(values_.size());
        values_.resize(values_.size() + plugins.Plugin(i).Schema().ncol, 0.0);
        instances_.push_back(plugins.Plugin(i).Create(*pb.Build()));
    }
    for (const auto& t : plugins.Samplers()) {
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
            instances_[i].Read(all.subspan(offsets_[i], instances_[i].Ncol()), adapter_.View());
        }
        values_fresh_ = true;
    }
    return values_;
}

void FeatureEngine::Advance(std::int64_t ts) {
    for (auto& s : samplers_) s->Advance(ts, [this] { return ReadAll(); });
}

void FeatureEngine::OnEvent(const nlib::mbo& m, nlib::qty_t filled) {
    values_fresh_ = false;
    if (m.action == nlib::action::clear) {
        const nq_control reset{NQ_CONTROL_RESET, m.ts};
        for (auto& inst : instances_) inst.OnControl(reset);
        for (auto& s : samplers_) s->ResetBaseline();
    } else {
        const nq_event e{m.ts, m.id, m.price, m.qty, filled,
                         static_cast<std::uint8_t>(m.side), static_cast<std::uint8_t>(m.action)};
        for (auto& inst : instances_) inst.OnEvent(e, adapter_.View());
    }
    for (auto& s : samplers_) {
        if (s->OnEvent(m.action)) s->Emit(m.ts, ReadAll());
    }
}

}  // namespace nq
