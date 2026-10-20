#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <nq/adapter_host.hpp>

#include <Adapter/AdapterEngine.h>
#include <Defaults.h>
#include <Feature/FeatureCatalog.h>
#include <PipelineTable.h>

namespace nq {

struct RunResult {
    bool interrupted = false;
    int adapter_status = 0;
    std::uint64_t consumed = 0;
    std::uint64_t stalls = 0;
};

class Session {
public:
    Session(const FeatureCatalog& catalog, const host::AdapterPlugin& adapter, const nq_params& adapter_params,
            std::size_t queue_capacity = defaults::kQueueCapacity,
            std::size_t expected_orders = defaults::kExpectedOrders);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    RunResult Run(const std::atomic<bool>& stop, std::size_t batch = defaults::kConsumeBatch);

    PipelineTable& Pipelines() noexcept { return table_; }
    const PipelineTable& Pipelines() const noexcept { return table_; }
    const AdapterEngine& Adapter() const noexcept { return adapter_; }

private:
    AdapterEngine adapter_;
    PipelineTable table_;
};

}  // namespace nq
