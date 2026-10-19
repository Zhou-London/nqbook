#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <nq/adapter_host.hpp>

#include <Feature/PluginSet.h>
#include <Market.h>
#include <QueueSink.h>

namespace nq {

struct RunResult {
    bool interrupted = false;
    int adapter_status = 0;
    std::uint64_t consumed = 0;
    std::uint64_t stalls = 0;
};

class Runner {
public:
    Runner(const PluginSet& plugins, const host::AdapterPlugin& adapter, const nq_params& adapter_params,
           std::size_t queue_capacity = 1 << 16, std::size_t expected_orders = 1024);

    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    RunResult Run(const std::atomic<bool>& stop, std::size_t batch = 4096);

    Market& GetMarket() noexcept { return market_; }
    const Market& GetMarket() const noexcept { return market_; }
    const host::AdapterInstance& Adapter() const noexcept { return adapter_; }

private:
    QueueSink sink_;
    host::AdapterInstance adapter_;
    Market market_;
};

}  // namespace nq
