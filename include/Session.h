#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <nq/adapter_host.hpp>

#include <Adapter/AdapterEngine.h>
#include <Defaults.h>
#include <Feature/FeatureCatalog.h>
#include <PipelineTable.h>
#include <Writer/ParquetWriter.h>
#include <Writer/WriterEngine.h>

namespace nq {

struct RunResult {
    bool interrupted = false;
    int adapter_status = 0;
    std::uint64_t consumed = 0;
    std::uint64_t stalls = 0;
    bool has_writer = false;
    std::uint64_t rows_written = 0;
    std::uint64_t writer_stalls = 0;
    std::string writer_error;
};

class Session {
public:
    Session(const FeatureCatalog& catalog, const host::AdapterPlugin& adapter, const nq_params& adapter_params,
            std::unique_ptr<ParquetWriter> writer = nullptr,
            std::size_t queue_capacity = defaults::kQueueCapacity,
            std::size_t expected_orders = defaults::kExpectedOrders);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    RunResult Run(const std::atomic<bool>& stop, std::size_t batch = defaults::kConsumeBatch);

    PipelineTable& Pipelines() noexcept { return table_; }
    const PipelineTable& Pipelines() const noexcept { return table_; }
    const AdapterEngine& Adapter() const noexcept { return adapter_; }
    const WriterEngine* Writer() const noexcept { return writer_ ? &*writer_ : nullptr; }

private:
    AdapterEngine adapter_;
    std::optional<WriterEngine> writer_;
    PipelineTable table_;
};

}  // namespace nq
