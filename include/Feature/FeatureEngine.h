#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <nlib/common.h>
#include <nq/host.hpp>

#include <Feature/BookAdapter.h>
#include <Feature/Sampler.h>
#include <Orderbook.h>

namespace nq {

struct InstrumentSpec {
    std::string symbol;
    std::int64_t tick_size = 1;
    std::int64_t lot_size = 1;
};

struct ColumnInfo {
    std::string name;
    std::string unit;
    std::string doc;
    std::uint8_t kind;
    std::size_t plugin_index;
    std::size_t local_index;
};

class FeatureEngine {
public:
    using KeyValues = std::vector<std::pair<std::string, std::string>>;

    FeatureEngine(const Orderbook& book, InstrumentSpec spec);

    FeatureEngine(const FeatureEngine&) = delete;
    FeatureEngine& operator=(const FeatureEngine&) = delete;

    std::size_t LoadPlugin(const std::string& path, std::string name = "", const KeyValues& kv = {});
    Sampler& AddSampler(SamplerSpec spec);

    void Advance(std::int64_t ts);
    void OnEvent(const nlib::mbo& m, nlib::qty_t filled);

    std::span<const double> ReadAll();

    const Orderbook& Book() const noexcept { return adapter_.Book(); }
    const InstrumentSpec& Instrument() const noexcept { return spec_; }
    const std::vector<ColumnInfo>& Columns() const noexcept { return columns_; }
    std::size_t PluginCount() const noexcept { return plugins_.size(); }
    const host::Plugin& Plugin(std::size_t i) const noexcept { return plugins_[i]; }
    std::size_t SamplerCount() const noexcept { return samplers_.size(); }
    Sampler& GetSampler(std::size_t i) noexcept { return *samplers_[i]; }
    const Sampler& GetSampler(std::size_t i) const noexcept { return *samplers_[i]; }
    Sampler* FindSampler(std::string_view name) noexcept;

private:
    std::size_t FindColumn(std::string_view name) const;

    BookAdapter adapter_;
    InstrumentSpec spec_;
    std::vector<host::Plugin> plugins_;
    std::vector<host::PluginInstance> instances_;
    std::vector<std::string> plugin_names_;
    std::vector<std::size_t> offsets_;
    std::vector<ColumnInfo> columns_;
    std::vector<double> values_;
    bool values_fresh_ = false;
    std::vector<std::unique_ptr<Sampler>> samplers_;
};

}  // namespace nq
