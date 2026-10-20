#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <unordered_set>

#include <nq/adapter_host.hpp>
#include <nq/instrument.hpp>

#include <Orderbook.h>

namespace nq {

class AdapterEngine {
public:
    AdapterEngine(host::AdapterInstance adapter, std::size_t queue_capacity);
    ~AdapterEngine();

    AdapterEngine(const AdapterEngine&) = delete;
    AdapterEngine& operator=(const AdapterEngine&) = delete;

    void Start();
    void Stop() noexcept;

    Orderbook::Queue& Queue() noexcept { return queue_; }
    bool Ended() const noexcept { return ended_.load(std::memory_order_acquire); }
    int Status() const noexcept { return status_.load(std::memory_order_acquire); }
    std::uint64_t Pushed() const noexcept { return pushed_.load(std::memory_order_relaxed); }
    std::uint64_t Stalls() const noexcept { return stalls_.load(std::memory_order_relaxed); }

    std::optional<Instrument> FindInstrument(std::uint32_t id) const;

private:
    void Run(std::stop_token st);
    void Discover(std::uint32_t id);
    bool Push(nq_mbo m, const std::stop_token& st) noexcept;

    host::AdapterInstance adapter_;
    Orderbook::Queue queue_;
    std::unordered_set<std::uint32_t> seen_;
    mutable std::mutex mu_;
    std::map<std::uint32_t, std::optional<Instrument>> instruments_;
    std::atomic<bool> ended_{false};
    std::atomic<int> status_{0};
    std::atomic<std::uint64_t> pushed_{0};
    std::atomic<std::uint64_t> stalls_{0};
    std::jthread thread_;
};

}  // namespace nq
