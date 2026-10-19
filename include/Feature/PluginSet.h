#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nq/host.hpp>

#include <Feature/Sampler.h>

namespace nq {

using KeyValues = std::vector<std::pair<std::string, std::string>>;

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

class PluginSet {
public:
    PluginSet() = default;
    PluginSet(const PluginSet&) = delete;
    PluginSet& operator=(const PluginSet&) = delete;

    std::size_t LoadPlugin(const std::string& path, std::string name = "", KeyValues params = {});
    void AddSampler(SamplerSpec spec);

    std::size_t PluginCount() const noexcept { return plugins_.size(); }
    const host::Plugin& Plugin(std::size_t i) const noexcept { return plugins_[i]; }
    const std::string& PluginName(std::size_t i) const noexcept { return names_[i]; }
    const KeyValues& PluginParams(std::size_t i) const noexcept { return params_[i]; }

    const std::vector<ColumnInfo>& Columns() const noexcept { return columns_; }
    const std::vector<SamplerTemplate>& Samplers() const noexcept { return samplers_; }
    const SamplerTemplate* FindSampler(std::string_view name) const noexcept;

private:
    std::size_t FindColumn(std::string_view name) const;

    std::vector<host::Plugin> plugins_;
    std::vector<std::string> names_;
    std::vector<KeyValues> params_;
    std::vector<ColumnInfo> columns_;
    std::vector<SamplerTemplate> samplers_;
};

}  // namespace nq
