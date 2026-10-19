#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <nq/adapter_host.hpp>

#include <Feature/FeatureCatalog.h>
#include <PipelineTable.h>
#include <QueueSink.h>

namespace nq {

struct RunResult {
    bool interrupted = false;
    int adapter_status = 0;
    std::uint64_t consumed = 0;
    std::uint64_t stalls = 0;
};

class Session {
public:
    Session(const FeatureCatalog& plugins, const host::AdapterPlugin& adapter, const nq_params& adapter_params,
           std::size_t queue_capacity = 1 << 16, std::size_t expected_orders = 1024);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    RunResult Run(const std::atomic<bool>& stop, std::size_t batch = 4096);

    PipelineTable& Pipelines() noexcept { return table_; }
    const PipelineTable& Pipelines() const noexcept { return table_; }
    const host::AdapterInstance& Adapter() const noexcept { return adapter_; }

private:
    QueueSink sink_;
    host::AdapterInstance adapter_;
    PipelineTable table_;
};

}  // namespace nq
