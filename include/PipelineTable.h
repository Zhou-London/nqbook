#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include <nlib/single_queue.h>

#include <Clock/TimerWheel.h>
#include <Defaults.h>
#include <Feature/FeatureCatalog.h>
#include <Pipeline.h>

namespace nq {

struct InstrumentStats {
    std::uint64_t events = 0;
    std::uint64_t ts_regressions = 0;
    std::int64_t first_ts = 0;
    std::int64_t last_ts = 0;
    bool resolved = true;
};

struct FeedStats {
    std::uint64_t events = 0;
    std::uint64_t ts_regressions = 0;
    std::uint64_t instrument_unknown = 0;
    std::int64_t first_ts = 0;
    std::int64_t last_ts = 0;
};

template <class Record>
class PipelineTable {
public:
    using Resolver = std::function<std::optional<Instrument>(std::uint32_t)>;

    struct Entry {
        std::uint32_t id = 0;
        std::uint32_t live_index = 0;
        std::unique_ptr<Pipeline<Record>> pipeline;
        InstrumentStats stats;
    };

    PipelineTable(const FeatureCatalog& catalog, Resolver resolver, PipelineOptions<Record> options = {},
                  std::size_t instrument_slots = defaults::kInstrumentSlots, RowSink* sink = nullptr);

    std::vector<Instrument> Instruments() const;

    PipelineTable(const PipelineTable&) = delete;
    PipelineTable& operator=(const PipelineTable&) = delete;

    void Apply(const Record& record);
    std::size_t Consume(nlib::single_queue<Record>& q, std::size_t max = std::numeric_limits<std::size_t>::max());
    void Close();

    Pipeline<Record>* Find(std::uint32_t instrument) noexcept;
    const Pipeline<Record>* Find(std::uint32_t instrument) const noexcept;
    const std::vector<Entry>& Entries() const noexcept { return entries_; }
    const std::vector<Pipeline<Record>*>& Live() const noexcept { return live_; }
    std::size_t InstrumentCount() const noexcept { return live_.size(); }
    const FeatureCatalog& Catalog() const noexcept { return *catalog_; }
    const FeedStats& Stats() const noexcept { return stats_; }

private:
    Entry& Resolve(std::uint32_t instrument);

    const FeatureCatalog* catalog_;
    Resolver resolver_;
    PipelineOptions<Record> options_;
    RowSink* sink_;
    std::vector<Entry> entries_;
    std::vector<Pipeline<Record>*> live_;
    TimerWheel timers_;
    FeedStats stats_;
};

}  // namespace nq
