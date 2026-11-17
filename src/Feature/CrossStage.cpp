#include <Feature/CrossStage.h>

#include <utility>

namespace nq {

namespace {

double* Gather(double* dst, const double* column, std::span<const std::uint32_t> rows) noexcept {
    for (const std::uint32_t row : rows) *dst++ = column[row];
    return dst;
}

}  // namespace

CrossStage::CrossStage(const CrossTemplate& cross, std::uint32_t table, RowSink* sink, std::uint64_t* epoch)
    : cross_(&cross),
      op_(cross.spec.make()),
      panel_(cross.inputs.size(), cross.spec.outputs.size()),
      sink_(sink),
      table_(table),
      interval_(cross.spec.every),
      fill_gaps_(cross.spec.fill_gaps),
      epoch_(epoch) {}

void CrossStage::Attach(std::uint32_t row, Engines engines) {
    const std::uint32_t before = panel_.RowCount();
    panel_.Ensure(row + 1);
    if (cross_->plugin) Bind(before, engines);
}

void CrossStage::Bind(std::uint32_t first, Engines engines) {
    if (panel_.Out(0) != bound_) {
        bound_ = panel_.Out(0);
        first = 0;
    }
    for (std::uint32_t row = first; row < panel_.RowCount(); ++row) {
        engines[row]->BindCross(*cross_->plugin, bound_ + row, panel_.Stride());
    }
}

void CrossStage::Advance(std::int64_t ts, Engines engines, Ids ids) {
    if (!started_) {
        started_ = true;
        due_ = BarAfter(ts);
        return;
    }
    if (ts < due_) return;
    if (fill_gaps_) {
        while (ts >= due_) {
            Fire(due_, engines, ids);
            due_ += interval_;
        }
        return;
    }
    if (!panel_.Changed().empty()) Fire(due_, engines, ids);
    due_ = BarAfter(ts);
}

void CrossStage::Close(Engines engines, Ids ids) {
    if (started_ && (fill_gaps_ || !panel_.Changed().empty())) Fire(due_, engines, ids);
}

void CrossStage::Fire(std::int64_t at, Engines engines, Ids ids) {
    const std::vector<std::size_t>& inputs = cross_->inputs;
    for (const std::uint32_t row : panel_.Changed()) {
        const std::span<const double> values = engines[row]->ReadAll();
        for (std::size_t c = 0; c < inputs.size(); ++c) panel_.In(c)[row] = values[inputs[c]];
        panel_.MarkValid(row);
    }
    const nq_cross_panel panel = panel_.View(at);
    op_->Apply(CrossView(&panel));
    if (epoch_ && cross_->plugin) ++*epoch_;
    Emit(at, ids);
    panel_.ClearChanged();
    ++fires_;
}

void CrossStage::Emit(std::int64_t at, Ids ids) {
    const std::span<const std::uint32_t> rows = fill_gaps_ ? panel_.ValidRows() : panel_.Changed();
    if (rows.empty()) return;

    FeatureBlock block = sink_ ? sink_->Acquire() : FeatureBlock{};
    block.table = table_;
    block.ts = at;
    block.instruments.resize(rows.size());
    block.values.resize(rows.size() * Ncol());
    for (std::size_t r = 0; r < rows.size(); ++r) block.instruments[r] = ids[rows[r]];

    double* dst = block.values.data();
    for (std::size_t c = 0; c < panel_.Nin(); ++c) dst = Gather(dst, panel_.In(c), rows);
    for (std::size_t c = 0; c < panel_.Nout(); ++c) dst = Gather(dst, panel_.Out(c), rows);

    if (sink_) sink_->Push(std::move(block));
    else blocks_.push_back(std::move(block));
}

}  // namespace nq
