#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nq/feature_host.hpp>
#include <nq/types.hpp>

#include <Feature/Cross.h>
#include <Feature/Sampler.h>

namespace nq {

using KeyValues = std::vector<std::pair<std::string, std::string>>;

class FeedMismatchError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

constexpr const char* FeedName(Feed feed) noexcept { return feed == Feed::OHLCV ? "OHLCV" : "MBO"; }

struct ColumnInfo {
    std::string name;
    std::string unit;
    std::string doc;
    std::uint8_t kind;
    std::size_t plugin_index;
    std::size_t local_index;
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
    std::size_t PluginCount() const noexcept { return plugins_.size(); }
    const host::Plugin& Plugin(std::size_t i) const noexcept { return plugins_[i]; }
    const std::string& PluginName(std::size_t i) const noexcept { return names_[i]; }
    const KeyValues& PluginParams(std::size_t i) const noexcept { return params_[i]; }

    const std::vector<ColumnInfo>& Columns() const noexcept { return columns_; }
    const std::vector<SamplerTemplate>& Samplers() const noexcept { return samplers_; }
    const SamplerTemplate* FindSampler(std::string_view name) const noexcept;
    const std::vector<CrossTemplate>& Crosses() const noexcept { return crosses_; }
    const CrossTemplate* FindCross(std::string_view name) const noexcept;

private:
    std::size_t FindColumn(std::string_view name) const;
    std::size_t FindPlugin(std::string_view name) const;

    /* ---------- Internal States ---------- */
    Feed feed_ = Feed::MBO;
    std::vector<host::Plugin> plugins_;
    std::vector<std::string> names_;
    std::vector<KeyValues> params_;
    std::vector<ColumnInfo> columns_;
    std::vector<SamplerTemplate> samplers_;
    std::vector<CrossTemplate> crosses_;
};

}  // namespace nq
