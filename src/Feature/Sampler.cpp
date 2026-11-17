#include <Feature/Sampler.h>

#include <utility>

namespace nq {

namespace {

AnyTrigger MakeTrigger(const TimeBars& bars) { return BarTrigger(bars); }
AnyTrigger MakeTrigger(const EveryEvents& events) { return CountTrigger(events.every, false); }
AnyTrigger MakeTrigger(const EveryTrades& trades) { return CountTrigger(trades.every, true); }

}  // namespace

Sampler::Sampler(const SamplerTrigger& trigger, std::vector<std::string> names, std::vector<SamplerColumn> columns,
                 RowSink* sink, std::uint32_t table, std::uint32_t instrument)
    : trigger_(std::visit([](const auto& t) { return MakeTrigger(t); }, trigger)),
      rows_(std::move(names)),
      row_(columns.size(), 0.0),
      sink_(sink),
      table_(table),
      instrument_(instrument) {
    sources_.reserve(columns.size());
    for (const SamplerColumn& c : columns) {
        if (c.kind == NQ_COLUMN_CUMULATIVE) deltas_.push_back(DeltaColumn{sources_.size()});
        sources_.push_back(c.index);
    }
}

void Sampler::Emit(std::int64_t ts, std::span<const double> values) {
    for (std::size_t i = 0; i < sources_.size(); ++i) row_[i] = values[sources_[i]];
    for (DeltaColumn& d : deltas_) {
        const double v = row_[d.slot];
        row_[d.slot] = v - d.baseline + d.carry;
        d.baseline = v;
        d.carry = 0.0;
    }
    if (!sink_) {
        rows_.Push(ts, row_);
        return;
    }
    FeatureBlock block = sink_->Acquire(table_);
    block.table = table_;
    block.ts = ts;
    block.instruments.assign(1, instrument_);
    block.values.assign(row_.begin(), row_.end());
    sink_->Push(std::move(block));
}

void Sampler::CarryDelta(std::span<const double> values) noexcept {
    for (DeltaColumn& d : deltas_) d.carry += values[sources_[d.slot]] - d.baseline;
}

void Sampler::ResetBaseline(std::span<const double> values) noexcept {
    for (DeltaColumn& d : deltas_) d.baseline = values[sources_[d.slot]];
}

}  // namespace nq
