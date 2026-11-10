#include <Feature/Sampler.h>

#include <utility>

namespace nq {

namespace {

AnyTrigger MakeTrigger(const SamplerSpec& spec) {
    switch (spec.trigger) {
        case Trigger::Time: return BarTrigger(spec.every, spec.fill_gaps);
        case Trigger::Events: return CountTrigger(spec.every, false);
        case Trigger::Trades: return CountTrigger(spec.every, true);
    }
    return CountTrigger(spec.every, false);
}

}  // namespace

Sampler::Sampler(SamplerSpec spec, std::vector<std::string> names, std::vector<SamplerColumn> columns,
                 RowSink* sink, std::uint32_t sampler_index, std::uint32_t instrument)
    : spec_(std::move(spec)),
      trigger_(MakeTrigger(spec_)),
      columns_(std::move(columns)),
      rows_(std::move(names)),
      last_(columns_.size(), 0.0),
      row_(columns_.size(), 0.0),
      sink_(sink),
      sampler_index_(sampler_index),
      instrument_(instrument) {}

void Sampler::Emit(std::int64_t ts, std::span<const double> values) {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        const double v = values[columns_[i].index];
        if (columns_[i].kind == NQ_COLUMN_CUMULATIVE) {
            row_[i] = v - last_[i];
            last_[i] = v;
        } else {
            row_[i] = v;
        }
    }
    if (sink_) sink_->Push(FeatureRow{sampler_index_, instrument_, ts, row_});
    else rows_.Push(ts, row_);
}

void Sampler::ResetBaseline() noexcept {
    for (double& v : last_) v = 0.0;
}

}  // namespace nq
