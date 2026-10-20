#include <PipelineTable.h>

#include <string>

namespace nq {

PipelineTable::PipelineTable(const FeatureCatalog& catalog, Resolver resolver, std::size_t expected_orders)
    : catalog_(&catalog), resolver_(std::move(resolver)), expected_orders_(expected_orders) {}

PipelineTable::Entry& PipelineTable::Resolve(std::uint32_t instrument) {
    auto it = entries_.find(instrument);
    if (it != entries_.end()) return it->second;

    std::optional<Instrument> spec = resolver_ ? resolver_(instrument) : std::nullopt;
    Entry entry;
    entry.stats.resolved = spec.has_value();
    if (!spec) {
        ++stats_.instrument_unknown;
        spec = Instrument{instrument, "#" + std::to_string(instrument), 1, 1};
    }
    entry.pipeline = std::make_unique<Pipeline>(*catalog_, std::move(*spec), expected_orders_);
    return entries_.emplace(instrument, std::move(entry)).first->second;
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

    for (auto& [id, other] : entries_) other.pipeline->Engine().Advance(m.ts);
    return e.pipeline->Apply(m);
}

void PipelineTable::Close() {
    for (auto& [id, e] : entries_) e.pipeline->Close();
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
    auto it = entries_.find(instrument);
    return it == entries_.end() ? nullptr : it->second.pipeline.get();
}

const Pipeline* PipelineTable::Find(std::uint32_t instrument) const noexcept {
    auto it = entries_.find(instrument);
    return it == entries_.end() ? nullptr : it->second.pipeline.get();
}

}  // namespace nq
