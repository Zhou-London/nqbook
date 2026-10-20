#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <thread>

#include <nlib/single_queue.h>
#include <nq/instrument.hpp>

#include <Writer/ParquetWriter.h>
#include <Writer/RowSink.h>

namespace nq {

class WriterEngine : public RowSink {
public:
    WriterEngine(std::unique_ptr<ParquetWriter> writer, std::size_t queue_capacity);
    ~WriterEngine() override;

    WriterEngine(const WriterEngine&) = delete;
    WriterEngine& operator=(const WriterEngine&) = delete;

    void Start();
    void Push(FeatureRow row) override;
    void Finish(std::span<const Instrument> instruments);
    void Abort() noexcept;

    const ParquetWriter& Writer() const noexcept { return *writer_; }
    bool Failed() const noexcept { return failed_.load(std::memory_order_acquire); }
    std::string Error() const;
    std::uint64_t Pushed() const noexcept { return pushed_.load(std::memory_order_relaxed); }
    std::uint64_t Written() const noexcept { return written_.load(std::memory_order_relaxed); }
    std::uint64_t Stalls() const noexcept { return stalls_.load(std::memory_order_relaxed); }

private:
    void Run(std::stop_token st);
    void Fail(const std::string& message);

    std::unique_ptr<ParquetWriter> writer_;
    nlib::single_queue<FeatureRow> queue_;
    std::atomic<bool> draining_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> abort_{false};
    std::atomic<std::uint64_t> pushed_{0};
    std::atomic<std::uint64_t> written_{0};
    std::atomic<std::uint64_t> stalls_{0};
    mutable std::mutex mu_;
    std::string error_;
    std::jthread thread_;
};

}  // namespace nq
