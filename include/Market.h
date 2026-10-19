#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>

#include <nlib/common.h>

#include <Feature/PluginSet.h>
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

class Market {
public:
    using Resolver = std::function<std::optional<InstrumentSpec>(std::uint32_t)>;

    struct Entry {
        std::unique_ptr<Pipeline> pipeline;
        InstrumentStats stats;
    };

    Market(const PluginSet& plugins, Resolver resolver, std::size_t expected_orders = 1024);

    Market(const Market&) = delete;
    Market& operator=(const Market&) = delete;

    nlib::qty_t Apply(const nlib::mbo& m);
    std::size_t Consume(Orderbook::Queue& q, std::size_t max = std::numeric_limits<std::size_t>::max());

    Pipeline* Find(std::uint32_t instrument) noexcept;
    const Pipeline* Find(std::uint32_t instrument) const noexcept;
    const std::map<std::uint32_t, Entry>& Entries() const noexcept { return entries_; }
    std::size_t InstrumentCount() const noexcept { return entries_.size(); }
    const PluginSet& Plugins() const noexcept { return *plugins_; }
    const FeedStats& Stats() const noexcept { return stats_; }

private:
    Entry& Resolve(std::uint32_t instrument);

    const PluginSet* plugins_;
    Resolver resolver_;
    std::size_t expected_orders_;
    std::map<std::uint32_t, Entry> entries_;
    FeedStats stats_;
};

}  // namespace nq
