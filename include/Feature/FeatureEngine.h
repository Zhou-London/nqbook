#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlib/common.h>
#include <nq/host.hpp>

#include <Feature/BookAdapter.h>
#include <Feature/PluginSet.h>
#include <Feature/Sampler.h>
#include <Orderbook.h>

namespace nq {

struct InstrumentSpec {
    std::string symbol;
    std::int64_t tick_size = 1;
    std::int64_t lot_size = 1;
};

class FeatureEngine {
public:
    FeatureEngine(const Orderbook& book, const PluginSet& plugins, InstrumentSpec spec);

    FeatureEngine(const FeatureEngine&) = delete;
    FeatureEngine& operator=(const FeatureEngine&) = delete;

    void Advance(std::int64_t ts);
    void OnEvent(const nlib::mbo& m, nlib::qty_t filled);

    std::span<const double> ReadAll();

    const Orderbook& Book() const noexcept { return adapter_.Book(); }
    const InstrumentSpec& Instrument() const noexcept { return spec_; }
    const PluginSet& Plugins() const noexcept { return *plugins_; }
    const std::vector<ColumnInfo>& Columns() const noexcept { return plugins_->Columns(); }
    std::size_t SamplerCount() const noexcept { return samplers_.size(); }
    Sampler& GetSampler(std::size_t i) noexcept { return *samplers_[i]; }
    const Sampler& GetSampler(std::size_t i) const noexcept { return *samplers_[i]; }
    Sampler* FindSampler(std::string_view name) noexcept;

private:
    BookAdapter adapter_;
    const PluginSet* plugins_;
    InstrumentSpec spec_;
    std::vector<host::PluginInstance> instances_;
    std::vector<std::size_t> offsets_;
    std::vector<double> values_;
    bool values_fresh_ = false;
    std::vector<std::unique_ptr<Sampler>> samplers_;
};

}  // namespace nq
