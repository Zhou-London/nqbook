#include <Feature/Sampler.h>

#include <utility>

namespace nq {

Sampler::Sampler(SamplerSpec spec, std::vector<std::string> names, std::vector<SamplerColumn> columns)
    : spec_(std::move(spec)),
      columns_(std::move(columns)),
      rows_(std::move(names)),
      last_(columns_.size(), 0.0),
      row_(columns_.size(), 0.0) {}

bool Sampler::OnEvent(nlib::action a) noexcept {
    switch (spec_.trigger) {
        case Trigger::Events: break;
        case Trigger::Trades:
            if (a != nlib::action::trade) return false;
            break;
        case Trigger::Time: return false;
    }
    if (++count_ < spec_.every) return false;
    count_ = 0;
    return true;
}

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
    rows_.Push(ts, row_);
}

void Sampler::ResetBaseline() noexcept {
    for (double& v : last_) v = 0.0;
}

}  // namespace nq
