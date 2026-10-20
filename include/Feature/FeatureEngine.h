#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlib/common.h>
#include <nq/feature_host.hpp>
#include <nq/instrument.hpp>

#include <Feature/OrderbookView.h>
#include <Feature/FeatureCatalog.h>
#include <Feature/Sampler.h>
#include <Orderbook.h>

namespace nq {

class FeatureEngine {
public:
    FeatureEngine(const Orderbook& book, const FeatureCatalog& catalog, Instrument spec);

    FeatureEngine(const FeatureEngine&) = delete;
    FeatureEngine& operator=(const FeatureEngine&) = delete;

    void Advance(std::int64_t ts);
    void OnEvent(const nq_mbo& m, nlib::qty_t filled);
    void Close();

    std::span<const double> ReadAll();

    const Orderbook& Book() const noexcept { return view_.Book(); }
    const nq::Instrument& GetInstrument() const noexcept { return spec_; }
    const FeatureCatalog& Catalog() const noexcept { return *catalog_; }
    const std::vector<ColumnInfo>& Columns() const noexcept { return catalog_->Columns(); }
    std::size_t SamplerCount() const noexcept { return samplers_.size(); }
    Sampler& GetSampler(std::size_t i) noexcept { return *samplers_[i]; }
    const Sampler& GetSampler(std::size_t i) const noexcept { return *samplers_[i]; }
    Sampler* FindSampler(std::string_view name) noexcept;

private:
    OrderbookView view_;
    const FeatureCatalog* catalog_;
    Instrument spec_;
    std::vector<host::PluginInstance> instances_;
    std::vector<std::size_t> offsets_;
    std::vector<double> values_;
    bool values_fresh_ = false;
    std::vector<std::unique_ptr<Sampler>> samplers_;
};

}  // namespace nq
