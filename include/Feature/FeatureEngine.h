#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <nlib/common.h>
#include <nq/feature_host.hpp>
#include <nq/instrument.hpp>

#include <Feature/FeatureCatalog.h>
#include <Feature/Sampler.h>
#include <Writer/RowSink.h>

namespace nq {

class FeatureEngine {
public:
    FeatureEngine(const FeatureCatalog& catalog, Instrument spec, RowSink* sink = nullptr,
                  const nq_book_view* book = nullptr);

    FeatureEngine(const FeatureEngine&) = delete;
    FeatureEngine& operator=(const FeatureEngine&) = delete;

    void Advance(std::int64_t ts);
    std::int64_t Due() const noexcept;
    void OnEvent(const nq_mbo& m, nlib::qty_t filled);
    void OnOhlcv(const nq_ohlcv& ohlcv);
    void OnWarmup(const nq_ohlcv& ohlcv) noexcept;
    void FinishWarmup();
    void Close();

    std::span<const double> ReadAll();
    void BindCross(std::size_t plugin, const double* row, std::size_t stride) noexcept {
        instances_[plugin].BindCross(row, stride);
    }
    void WatchCross(const std::uint64_t& epoch) noexcept { cross_epoch_ = &epoch; }

    const nq::Instrument& GetInstrument() const noexcept { return spec_; }
    std::size_t SamplerCount() const noexcept { return samplers_.size(); }
    Sampler& GetSampler(std::size_t i) noexcept { return samplers_[i]; }
    const Sampler& GetSampler(std::size_t i) const noexcept { return samplers_[i]; }

private:
    static constexpr std::uint64_t kStale = ~std::uint64_t{0};
    static constexpr std::uint64_t kNoCross = 0;

    void ResetPlugins(std::int64_t ts);
    void NotifySamplers(std::int64_t ts, bool is_trade);

    const nq_book_view* book_;
    Instrument spec_;
    std::vector<host::PluginInstance> instances_;
    std::vector<double> values_;
    const std::uint64_t* cross_epoch_ = &kNoCross;
    std::uint64_t values_epoch_ = kStale;
    std::vector<Sampler> samplers_;
};

}  // namespace nq
