#include <Feature/FeatureEngine.h>

#include <algorithm>

namespace nq {

FeatureEngine::FeatureEngine(const FeatureCatalog& catalog, Instrument spec, RowSink* sink, const nq_book_view* book)
    : book_(book), spec_(std::move(spec)) {
    instances_.reserve(catalog.Plugins().size());
    for (const LoadedPlugin& p : catalog.Plugins()) {
        ParamsBuilder pb = ScaledParams(p.params);
        pb.Symbol(spec_.symbol).TickSize(spec_.tick_size).LotSize(spec_.lot_size);
        values_.resize(values_.size() + p.library.Schema().ncol, 0.0);
        instances_.push_back(p.library.Create(*pb.Build()));
    }
    if (sink) sink->Register(spec_);
    samplers_.reserve(catalog.Samplers().size());
    for (std::size_t i = 0; i < catalog.Samplers().size(); ++i) {
        const auto& t = catalog.Samplers()[i];
        samplers_.emplace_back(t.spec.trigger, t.names, t.columns, sink, catalog.SamplerTable(i), spec_.id);
    }
}

std::span<const double> FeatureEngine::ReadAll() {
    if (values_epoch_ != *cross_epoch_) {
        std::span<double> rest(values_);
        for (auto& inst : instances_) {
            const std::size_t ncol = inst.Ncol();
            inst.Read(rest.first(ncol), book_);
            rest = rest.subspan(ncol);
        }
        values_epoch_ = *cross_epoch_;
    }
    return values_;
}

void FeatureEngine::Advance(std::int64_t ts) {
    for (auto& s : samplers_) s.Advance(ts, [this] { return ReadAll(); });
}

std::int64_t FeatureEngine::Due() const noexcept {
    std::int64_t due = TimerWheel::kNever;
    for (const auto& s : samplers_) due = std::min(due, s.Due());
    return due;
}

void FeatureEngine::Close() {
    for (auto& s : samplers_) s.Close([this] { return ReadAll(); });
}

void FeatureEngine::ResetPlugins(std::int64_t ts) {
    const std::span<const double> before = ReadAll();
    for (auto& s : samplers_) s.CarryDelta(before);

    const nq_control reset{NQ_CONTROL_RESET, ts};
    for (auto& inst : instances_) inst.OnControl(reset);
    values_epoch_ = kStale;

    const std::span<const double> after = ReadAll();
    for (auto& s : samplers_) s.ResetBaseline(after);
}

void FeatureEngine::OnEvent(const nq_mbo& m, nlib::qty_t filled) {
    values_epoch_ = kStale;
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
    values_epoch_ = kStale;
    for (auto& inst : instances_) inst.OnOhlcv(ohlcv);
    NotifySamplers(ohlcv.ts, false);
}

void FeatureEngine::OnWarmup(const nq_ohlcv& ohlcv) noexcept {
    values_epoch_ = kStale;
    for (auto& inst : instances_) inst.OnWarmup(ohlcv);
}

void FeatureEngine::FinishWarmup() {
    const std::span<const double> warmed = ReadAll();
    for (auto& s : samplers_) s.ResetBaseline(warmed);
}

void FeatureEngine::NotifySamplers(std::int64_t ts, bool is_trade) {
    for (auto& s : samplers_) s.OnEvent(ts, is_trade, [this] { return ReadAll(); });
}

}  // namespace nq
