#include <PipelineTable.h>

#include <algorithm>
#include <string>

namespace nq {

PipelineTable::PipelineTable(const FeatureCatalog& catalog, Resolver resolver, std::size_t expected_orders,
                             std::size_t instrument_slots, RowSink* sink)
    : catalog_(&catalog), resolver_(std::move(resolver)), expected_orders_(expected_orders), sink_(sink) {
    entries_.resize(instrument_slots);
    for (std::size_t i = 0; i < entries_.size(); ++i) entries_[i].id = static_cast<std::uint32_t>(i);
}

PipelineTable::Entry& PipelineTable::Resolve(std::uint32_t instrument) {
    if (instrument >= entries_.size()) {
        const std::size_t old = entries_.size();
        entries_.resize(std::max<std::size_t>(instrument + 1, old * 2));
        for (std::size_t i = old; i < entries_.size(); ++i) entries_[i].id = static_cast<std::uint32_t>(i);
    }
    Entry& entry = entries_[instrument];
    if (entry.pipeline) return entry;

    std::optional<Instrument> spec = resolver_ ? resolver_(instrument) : std::nullopt;
    entry.stats.resolved = spec.has_value();
    if (!spec) {
        ++stats_.instrument_unknown;
        spec = Instrument{instrument, "#" + std::to_string(instrument), 1, 1};
    }
    entry.pipeline = std::make_unique<Pipeline>(*catalog_, std::move(*spec), expected_orders_, sink_);
    live_.push_back(entry.pipeline.get());
    return entry;
}

nlib::qty_t PipelineTable::Apply(const nq_mbo& m) {
    Entry& e = Resolve(m.instrument);

    if (stats_.events == 0) stats_.first_ts = m.ts;
    else if (m.ts < stats_.last_ts) ++stats_.ts_regressions;
    stats_.last_ts = m.ts;
    ++stats_.events;

    if (e.stats.events == 0) e.stats.first_ts = m.ts;
    else if (m.ts < e.stats.last_ts) ++e.stats.ts_regressions;
    e.stats.last_ts = m.ts;
    ++e.stats.events;

    for (Pipeline* p : live_) p->Engine().Advance(m.ts);
    return e.pipeline->Apply(m);
}

std::vector<Instrument> PipelineTable::Instruments() const {
    std::vector<Instrument> out;
    out.reserve(live_.size());
    for (const Entry& e : entries_) {
        if (e.pipeline) out.push_back(e.pipeline->Engine().GetInstrument());
    }
    return out;
}

void PipelineTable::Close() {
    for (Pipeline* p : live_) p->Close();
}

std::size_t PipelineTable::Consume(Orderbook::Queue& q, std::size_t max) {
    std::size_t n = 0;
    while (n < max) {
        auto m = q.try_pop();
        if (!m) break;
        Apply(*m);
        ++n;
    }
    return n;
}

Pipeline* PipelineTable::Find(std::uint32_t instrument) noexcept {
    return instrument < entries_.size() ? entries_[instrument].pipeline.get() : nullptr;
}

const Pipeline* PipelineTable::Find(std::uint32_t instrument) const noexcept {
    return instrument < entries_.size() ? entries_[instrument].pipeline.get() : nullptr;
}

}  // namespace nq
