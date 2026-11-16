#include <PipelineTable.h>

#include <algorithm>
#include <string>

namespace nq {

template <class Record>
PipelineTable<Record>::PipelineTable(const FeatureCatalog& catalog, Resolver resolver, PipelineOptions<Record> options,
                                     std::size_t instrument_slots, RowSink* sink)
    : catalog_(&catalog), resolver_(std::move(resolver)), options_(options), sink_(sink) {
    entries_.resize(instrument_slots);
    for (std::size_t i = 0; i < entries_.size(); ++i) entries_[i].id = static_cast<std::uint32_t>(i);
}

template <class Record>
typename PipelineTable<Record>::Entry& PipelineTable<Record>::Resolve(std::uint32_t instrument) {
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
    entry.pipeline = std::make_unique<Pipeline<Record>>(*catalog_, std::move(*spec), options_, sink_);
    entry.live_index = static_cast<std::uint32_t>(live_.size());
    live_.push_back(entry.pipeline.get());
    return entry;
}

template <class Record>
void PipelineTable<Record>::Apply(const Record& record) {
    Entry& e = Resolve(record.instrument);

    if (stats_.events == 0) stats_.first_ts = record.ts;
    else if (record.ts < stats_.last_ts) ++stats_.ts_regressions;
    stats_.last_ts = record.ts;
    ++stats_.events;

    if (e.stats.events == 0) e.stats.first_ts = record.ts;
    else if (record.ts < e.stats.last_ts) ++e.stats.ts_regressions;
    e.stats.last_ts = record.ts;
    ++e.stats.events;

    timers_.AdvanceTo(record.ts, [&](std::uint32_t key) {
        FeatureEngine& woken = live_[key]->Engine();
        woken.Advance(record.ts);
        timers_.WakeBy(key, woken.Due());
    });
    FeatureEngine& engine = e.pipeline->Engine();
    engine.Advance(record.ts);
    e.pipeline->Apply(record);
    timers_.WakeBy(e.live_index, engine.Due());
}

template <class Record>
std::vector<Instrument> PipelineTable<Record>::Instruments() const {
    std::vector<Instrument> out;
    out.reserve(live_.size());
    for (const Entry& e : entries_) {
        if (e.pipeline) out.push_back(e.pipeline->Engine().GetInstrument());
    }
    return out;
}

template <class Record>
void PipelineTable<Record>::Close() {
    for (Pipeline<Record>* p : live_) p->Close();
}

template <class Record>
std::size_t PipelineTable<Record>::Consume(nlib::single_queue<Record>& q, std::size_t max) {
    std::size_t n = 0;
    while (n < max) {
        auto record = q.try_pop();
        if (!record) break;
        Apply(*record);
        ++n;
    }
    return n;
}

template <class Record>
Pipeline<Record>* PipelineTable<Record>::Find(std::uint32_t instrument) noexcept {
    return instrument < entries_.size() ? entries_[instrument].pipeline.get() : nullptr;
}

template <class Record>
const Pipeline<Record>* PipelineTable<Record>::Find(std::uint32_t instrument) const noexcept {
    return instrument < entries_.size() ? entries_[instrument].pipeline.get() : nullptr;
}

template class PipelineTable<nq_mbo>;
template class PipelineTable<nq_ohlcv>;

}  // namespace nq
