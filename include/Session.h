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

struct WriterResult {
    std::uint64_t rows = 0;
    std::uint64_t stalls = 0;
    std::string error;
};

struct RunResult {
    bool interrupted = false;
    int adapter_status = 0;
    std::uint64_t consumed = 0;
    std::uint64_t stalls = 0;
    std::optional<WriterResult> writer;
};

template <class Record>
class Session {
public:
    /* ---------- CTR/DTRs ---------- */
    Session(const FeatureCatalog& catalog, const host::AdapterPlugin& adapter, const nq_params& adapter_params,
            std::unique_ptr<ParquetWriter> writer = nullptr,
            std::size_t queue_capacity = defaults::kQueueCapacity);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /* ---------- Member Functions ---------- */
    RunResult Run(const std::atomic<bool>& stop);

    /* ---------- Getters ---------- */
    const PipelineTable<Record>& Pipelines() const noexcept { return table_; }
    const WriterEngine* Writer() const noexcept { return writer_ ? &*writer_ : nullptr; }

private:
    /* ---------- Internal States ---------- */
    AdapterEngine<Record> adapter_;
    std::optional<WriterEngine> writer_;
    PipelineTable<Record> table_;
};

}  // namespace nq
