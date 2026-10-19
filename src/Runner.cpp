#include <Runner.h>

#include <thread>

namespace nq {

Runner::Runner(const PluginSet& plugins, const host::AdapterPlugin& adapter, const nq_params& adapter_params,
               std::size_t queue_capacity, std::size_t expected_orders)
    : sink_(queue_capacity),
      adapter_(adapter.Create(adapter_params, sink_.Raw())),
      market_(
          plugins,
          [this](std::uint32_t id) -> std::optional<InstrumentSpec> {
              auto i = adapter_.Instrument(id);
              if (!i) return std::nullopt;
              return InstrumentSpec{i->symbol ? i->symbol : "", i->tick_size, i->lot_size};
          },
          expected_orders) {}

RunResult Runner::Run(const std::atomic<bool>& stop, std::size_t batch) {
    RunResult result;
    adapter_.Start();
    Orderbook::Queue& q = sink_.Queue();
    for (;;) {
        const std::size_t n = market_.Consume(q, batch);
        result.consumed += n;
        if (n > 0) {
            if (stop.load(std::memory_order_relaxed)) {
                result.interrupted = true;
                break;
            }
            continue;
        }
        if (sink_.Ended() && q.empty()) break;
        if (stop.load(std::memory_order_relaxed)) {
            result.interrupted = true;
            break;
        }
        std::this_thread::yield();
    }
    if (result.interrupted) sink_.Abort();
    adapter_.Stop();
    if (!result.interrupted) result.consumed += market_.Consume(q);
    result.adapter_status = sink_.Ended() ? sink_.Status() : 0;
    result.stalls = sink_.Stalls();
    return result;
}

}  // namespace nq
