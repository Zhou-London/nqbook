#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <Feature/BarClock.h>
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

    CrossStage(const CrossTemplate& cross, std::uint32_t table, RowSink* sink, std::uint64_t& epoch);

    const CrossPanel& Panel() const noexcept { return panel_; }
    const std::vector<FeatureBlock>& Blocks() const noexcept { return blocks_; }
    std::size_t Ncol() const noexcept { return cross_->names.size(); }
    std::uint64_t Fires() const noexcept { return fires_; }

    void Attach(std::uint32_t row, Engines engines);
    void Touch(std::uint32_t row) { panel_.Touch(row); }
    void Advance(std::int64_t ts, Engines engines, Ids ids) {
        clock_.Advance(ts, HasChanges(), [&](std::int64_t at) { Fire(at, engines, ids); });
    }
    void Close(Engines engines, Ids ids) {
        if (clock_.Armed(HasChanges())) Fire(clock_.Next(), engines, ids);
    }

private:
    bool HasChanges() const noexcept { return !panel_.Changed().empty(); }
    void Fire(std::int64_t at, Engines engines, Ids ids);
    void Emit(std::int64_t at, Ids ids);
    void Bind(std::uint32_t first, Engines engines);

    const CrossTemplate* cross_;
    std::unique_ptr<CrossOperator> op_;
    CrossPanel panel_;
    std::vector<FeatureBlock> blocks_;
    RowSink* sink_;
    std::uint32_t table_;
    BarClock clock_;
    std::uint64_t fires_ = 0;
    std::uint64_t* epoch_;
};

}  // namespace nq
