#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <nq/adapter_abi.h>

#include <Orderbook.h>

namespace nq {

class QueueSink {
public:
    explicit QueueSink(std::size_t capacity);

    QueueSink(const QueueSink&) = delete;
    QueueSink& operator=(const QueueSink&) = delete;

    const nq_sink& Raw() const noexcept { return sink_; }
    Orderbook::Queue& Queue() noexcept { return queue_; }

    bool Ended() const noexcept { return ended_.load(std::memory_order_acquire); }
    int Status() const noexcept { return status_.load(std::memory_order_acquire); }
    std::uint64_t Pushed() const noexcept { return pushed_.load(std::memory_order_relaxed); }
    std::uint64_t Stalls() const noexcept { return stalls_.load(std::memory_order_relaxed); }

    void Abort() noexcept { abort_.store(true, std::memory_order_release); }

private:
    static void Push(void* ctx, const nq_mbo* m) noexcept;
    static void End(void* ctx, int status) noexcept;

    Orderbook::Queue queue_;
    nq_sink sink_;
    std::atomic<bool> ended_{false};
    std::atomic<bool> abort_{false};
    std::atomic<int> status_{0};
    std::atomic<std::uint64_t> pushed_{0};
    std::atomic<std::uint64_t> stalls_{0};
};

}  // namespace nq
