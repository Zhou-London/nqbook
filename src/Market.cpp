#include <Market.h>

#include <string>

namespace nq {

Market::Market(const PluginSet& plugins, Resolver resolver, std::size_t expected_orders)
    : plugins_(&plugins), resolver_(std::move(resolver)), expected_orders_(expected_orders) {}

Market::Entry& Market::Resolve(std::uint32_t instrument) {
    auto it = entries_.find(instrument);
    if (it != entries_.end()) return it->second;

    std::optional<InstrumentSpec> spec = resolver_ ? resolver_(instrument) : std::nullopt;
    Entry entry;
    entry.stats.resolved = spec.has_value();
    if (!spec) {
        ++stats_.instrument_unknown;
        spec = InstrumentSpec{"#" + std::to_string(instrument), 1, 1};
    }
    entry.pipeline = std::make_unique<Pipeline>(*plugins_, std::move(*spec), expected_orders_);
    return entries_.emplace(instrument, std::move(entry)).first->second;
}

nlib::qty_t Market::Apply(const nlib::mbo& m) {
    Entry& e = Resolve(m.instrument);

    if (stats_.events == 0) stats_.first_ts = m.ts;
    else if (m.ts < stats_.last_ts) ++stats_.ts_regressions;
    stats_.last_ts = m.ts;
    ++stats_.events;

    if (e.stats.events == 0) e.stats.first_ts = m.ts;
    else if (m.ts < e.stats.last_ts) ++e.stats.ts_regressions;
    e.stats.last_ts = m.ts;
    ++e.stats.events;

    return e.pipeline->Apply(m);
}

std::size_t Market::Consume(Orderbook::Queue& q, std::size_t max) {
    std::size_t n = 0;
    while (n < max) {
        auto m = q.try_pop();
        if (!m) break;
        Apply(*m);
        ++n;
    }
    return n;
}

Pipeline* Market::Find(std::uint32_t instrument) noexcept {
    auto it = entries_.find(instrument);
    return it == entries_.end() ? nullptr : it->second.pipeline.get();
}

const Pipeline* Market::Find(std::uint32_t instrument) const noexcept {
    auto it = entries_.find(instrument);
    return it == entries_.end() ? nullptr : it->second.pipeline.get();
}

}  // namespace nq
