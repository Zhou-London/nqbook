#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <variant>

#include <nlib/single_queue.h>
#include <nq/instrument.hpp>

#include <Writer/ParquetWriter.h>
#include <Writer/RowSink.h>

namespace nq {

class WriterEngine : public RowSink {
public:
    WriterEngine(std::unique_ptr<ParquetWriter> writer, std::size_t queue_capacity);

    WriterEngine(const WriterEngine&) = delete;
    WriterEngine& operator=(const WriterEngine&) = delete;

    void Register(const Instrument& instrument) override;
    void Push(FeatureBlock block) override;
    FeatureBlock Acquire(std::uint32_t table) override;
    const std::string& Finish();

    const ParquetWriter& Writer() const noexcept { return *writer_; }
    std::uint64_t Written() const noexcept { return written_.load(std::memory_order_relaxed); }
    std::uint64_t Stalls() const noexcept { return stalls_.load(std::memory_order_relaxed); }

private:
    using Message = std::variant<FeatureBlock, Instrument>;

    void Enqueue(Message message);
    void Recycle(FeatureBlock block);
    void Run(std::stop_token st);

    std::unique_ptr<ParquetWriter> writer_;
    nlib::single_queue<Message> queue_;
    std::deque<nlib::single_queue<FeatureBlock>> spare_by_table_;
    std::atomic<bool> failed_{false};
    std::atomic<std::uint64_t> written_{0};
    std::atomic<std::uint64_t> stalls_{0};
    std::string error_;
    std::jthread thread_;
};

}  // namespace nq
