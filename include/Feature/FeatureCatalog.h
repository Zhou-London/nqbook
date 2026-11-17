#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nq/feature_host.hpp>
#include <nq/params.hpp>
#include <nq/types.hpp>

#include <Feature/Cross.h>
#include <Feature/Sampler.h>
#include <KeyValues.h>

namespace nq {

class FeedMismatchError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

constexpr const char* FeedName(Feed feed) noexcept { return feed == Feed::OHLCV ? "OHLCV" : "MBO"; }

template <class Loaded>
Feed FeedOf(const Loaded& loaded) noexcept {
    return static_cast<Feed>(loaded.Schema().feed);
}

struct LoadedPlugin {
    host::Plugin library;
    std::string name;
    KeyValues params;
};

struct ColumnInfo {
    std::string name;
    std::string unit;
    std::string doc;
    std::uint8_t kind;
    std::size_t plugin_index;
};

struct SamplerTemplate {
    SamplerSpec spec;
    std::vector<std::string> names;
    std::vector<SamplerColumn> columns;
};

struct CrossTemplate {
    CrossSpec spec;
    std::vector<std::size_t> inputs;
    std::vector<std::string> names;
    std::optional<std::size_t> plugin;
};

struct TableSchema {
    const std::string& name;
    const std::vector<std::string>& columns;
};

class FeatureCatalog {
public:
    /* ---------- CTR/DTRs ---------- */
    FeatureCatalog() = default;
    FeatureCatalog(const FeatureCatalog&) = delete;
    FeatureCatalog& operator=(const FeatureCatalog&) = delete;

    /* ---------- Member Functions ---------- */
    std::size_t LoadPlugin(const std::string& path, std::string name = "", KeyValues params = {});
    void AddSampler(SamplerSpec spec);
    void AddCross(CrossSpec spec);
    void AddCross(std::string name, std::string_view plugin, std::int64_t every, bool fill_gaps = false);
    void RequireFeed(Feed adapter_feed) const;

    /* ---------- Getters ---------- */
    Feed GetFeed() const noexcept { return feed_; }
    const std::vector<LoadedPlugin>& Plugins() const noexcept { return plugins_; }
    const std::vector<ColumnInfo>& Columns() const noexcept { return columns_; }
    const std::vector<SamplerTemplate>& Samplers() const noexcept { return samplers_; }
    const SamplerTemplate* FindSampler(std::string_view name) const noexcept;
    const std::vector<CrossTemplate>& Crosses() const noexcept { return crosses_; }
    const CrossTemplate* FindCross(std::string_view name) const noexcept;
    std::size_t TableCount() const noexcept { return samplers_.size() + crosses_.size(); }
    std::uint32_t SamplerTable(std::size_t i) const noexcept { return static_cast<std::uint32_t>(i); }
    std::uint32_t CrossTable(std::size_t i) const noexcept { return static_cast<std::uint32_t>(samplers_.size() + i); }
    TableSchema Table(std::size_t t) const noexcept;

private:
    std::size_t FindColumn(std::string_view name) const;
    std::size_t FindPlugin(std::string_view name) const;
    void RegisterCross(CrossSpec spec, std::optional<std::size_t> plugin);

    /* ---------- Internal States ---------- */
    Feed feed_ = Feed::MBO;
    std::vector<LoadedPlugin> plugins_;
    std::vector<ColumnInfo> columns_;
    std::vector<SamplerTemplate> samplers_;
    std::vector<CrossTemplate> crosses_;
};

}  // namespace nq
