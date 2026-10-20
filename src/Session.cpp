#include <Session.h>

#include <thread>

namespace nq {

Session::Session(const FeatureCatalog& catalog, const host::AdapterPlugin& adapter, const nq_params& adapter_params,
                 std::size_t queue_capacity, std::size_t expected_orders)
    : adapter_(adapter.Create(adapter_params), queue_capacity),
      table_(
          catalog, [this](std::uint32_t id) { return adapter_.FindInstrument(id); }, expected_orders) {}

RunResult Session::Run(const std::atomic<bool>& stop, std::size_t batch) {
    RunResult result;
    adapter_.Start();
    Orderbook::Queue& q = adapter_.Queue();
    for (;;) {
        const std::size_t n = table_.Consume(q, batch);
        result.consumed += n;
        if (n > 0) {
            if (stop.load(std::memory_order_relaxed)) {
                result.interrupted = true;
                break;
            }
            continue;
        }
        if (adapter_.Ended() && q.empty()) break;
        if (stop.load(std::memory_order_relaxed)) {
            result.interrupted = true;
            break;
        }
        std::this_thread::yield();
    }
    adapter_.Stop();
    if (!result.interrupted) result.consumed += table_.Consume(q);
    table_.Close();
    result.adapter_status = adapter_.Ended() ? adapter_.Status() : 0;
    result.stalls = adapter_.Stalls();
    return result;
}

}  // namespace nq
