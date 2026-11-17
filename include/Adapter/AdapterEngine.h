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

#include <nlib/single_queue.h>
#include <nq/adapter_host.hpp>
#include <nq/instrument.hpp>

namespace nq {

template <class Record>
class AdapterEngine {
public:
    /* ---------- CTR/DTRs ---------- */
    AdapterEngine(host::AdapterInstance adapter, std::size_t queue_capacity);

    AdapterEngine(const AdapterEngine&) = delete;
    AdapterEngine& operator=(const AdapterEngine&) = delete;

    /* ---------- Member Functions ---------- */
    void Start();
    void Stop() noexcept;
    std::optional<Instrument> FindInstrument(std::uint32_t id) const;

    /* ---------- Getters ---------- */
    nlib::single_queue<Record>& Queue() noexcept { return queue_; }
    bool Ended() const noexcept { return ended_.load(std::memory_order_acquire); }
    int Status() const noexcept { return status_.load(std::memory_order_acquire); }
    std::uint64_t Stalls() const noexcept { return stalls_.load(std::memory_order_relaxed); }

private:
    /* ---------- Internal Functions ---------- */
    void Run(std::stop_token st);
    void Discover(std::uint32_t id);
    bool Push(Record record, const std::stop_token& st) noexcept;

    /* ---------- Internal States ---------- */
    host::AdapterInstance adapter_;
    nlib::single_queue<Record> queue_;
    std::unordered_set<std::uint32_t> seen_;
    mutable std::mutex mu_;
    std::map<std::uint32_t, Instrument> instruments_;
    std::atomic<bool> ended_{false};
    std::atomic<int> status_{0};
    std::atomic<std::uint64_t> stalls_{0};
    std::jthread thread_;
};

}  // namespace nq
