#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#include <Feature/Cross.h>
#include <Feature/CrossPanel.h>
#include <Feature/FeatureCatalog.h>
#include <Feature/FeatureEngine.h>
#include <Writer/RowSink.h>

namespace nq {

class CrossStage {
public:
    using Engines = std::span<FeatureEngine* const>;
    using Ids = std::span<const std::uint32_t>;

    CrossStage(const CrossTemplate& cross, std::uint32_t table, RowSink* sink = nullptr,
               std::uint64_t* epoch = nullptr);

    const CrossSpec& Spec() const noexcept { return cross_->spec; }
    const CrossPanel& Panel() const noexcept { return panel_; }
    const std::vector<FeatureBlock>& Blocks() const noexcept { return blocks_; }
    std::size_t Ncol() const noexcept { return cross_->names.size(); }
    std::uint64_t Fires() const noexcept { return fires_; }

    std::int64_t Due() const noexcept { return due_; }
    void Attach(std::uint32_t row, Engines engines);
    void Touch(std::uint32_t row) { panel_.Touch(row); }
    void Advance(std::int64_t ts, Engines engines, Ids ids);
    void Close(Engines engines, Ids ids);

private:
    std::int64_t BarAfter(std::int64_t ts) const noexcept { return (ts / interval_ + 1) * interval_; }
    void Fire(std::int64_t at, Engines engines, Ids ids);
    void Emit(std::int64_t at, Ids ids);
    void Bind(std::uint32_t first, Engines engines);

    const CrossTemplate* cross_;
    std::unique_ptr<CrossOperator> op_;
    CrossPanel panel_;
    std::vector<FeatureBlock> blocks_;
    RowSink* sink_;
    std::uint32_t table_;
    std::int64_t interval_;
    bool fill_gaps_;
    bool started_ = false;
    std::int64_t due_ = std::numeric_limits<std::int64_t>::min();
    std::uint64_t fires_ = 0;
    const double* bound_ = nullptr;
    std::uint64_t* epoch_;
};

}  // namespace nq
