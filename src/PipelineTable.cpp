#include <PipelineTable.h>

#include <algorithm>
#include <string>

namespace nq {

template <class Record>
PipelineTable<Record>::PipelineTable(const FeatureCatalog& catalog, Resolver resolver, PipelineOptions<Record> options,
                                     std::size_t instrument_slots, RowSink* sink)
    : catalog_(&catalog), resolver_(std::move(resolver)), options_(options), sink_(sink) {
    entries_.resize(instrument_slots);

    crosses_.reserve(catalog.Crosses().size());
    for (std::size_t i = 0; i < catalog.Crosses().size(); ++i) {
        crosses_.emplace_back(catalog.Crosses()[i], catalog.CrossTable(i), sink, cross_epoch_);
    }
}

template <class Record>
typename PipelineTable<Record>::Entry& PipelineTable<Record>::Resolve(std::uint32_t instrument) {
    if (instrument >= entries_.size()) entries_.resize(std::max<std::size_t>(instrument + 1, entries_.size() * 2));
    Entry& entry = entries_[instrument];
    if (entry.pipeline) return entry;

    std::optional<Instrument> spec = resolver_ ? resolver_(instrument) : std::nullopt;
    entry.stats.resolved = spec.has_value();
    if (!spec) {
        ++stats_.instrument_unknown;
        spec = Instrument{instrument, "#" + std::to_string(instrument), 1, 1};
    }
    entry.pipeline = std::make_unique<Pipeline<Record>>(*catalog_, std::move(*spec), options_, sink_);
    entry.live_index = static_cast<std::uint32_t>(engines_.size());
    engines_.push_back(&entry.pipeline->Engine());
    live_ids_.push_back(instrument);
    entry.pipeline->Engine().WatchCross(cross_epoch_);
    for (CrossStage& c : crosses_) c.Attach(entry.live_index, engines_);
    return entry;
}

template <class Record>
void PipelineTable<Record>::Apply(const Record& record) {
    Entry& e = Resolve(record.instrument);

    stats_.Observe(record.ts);
    e.stats.Observe(record.ts);

    for (CrossStage& c : crosses_) c.Advance(record.ts, engines_, live_ids_);
    timers_.AdvanceTo(record.ts, [&](std::uint32_t key) {
        FeatureEngine& woken = *engines_[key];
        woken.Advance(record.ts);
        timers_.WakeBy(key, woken.Due());
    });
    FeatureEngine& engine = e.pipeline->Engine();
    engine.Advance(record.ts);
    e.pipeline->Apply(record);
    timers_.WakeBy(e.live_index, engine.Due());
    for (CrossStage& c : crosses_) c.Touch(e.live_index);
}

template <class Record>
void PipelineTable<Record>::Close() {
    for (CrossStage& c : crosses_) c.Close(engines_, live_ids_);
    for (FeatureEngine* engine : engines_) engine->Close();
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
