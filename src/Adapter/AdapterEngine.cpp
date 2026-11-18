#include <Adapter/AdapterEngine.h>

#include <utility>

namespace nq {

template <class Record>
AdapterEngine<Record>::AdapterEngine(host::AdapterInstance adapter, std::size_t queue_capacity)
    : adapter_(std::move(adapter)), queue_(queue_capacity) {}

template <class Record>
void AdapterEngine<Record>::Warmup(const std::atomic<bool>& stop, const std::function<void(const nq_ohlcv&)>& on_bar) {
    nq_ohlcv bar;

    while (!stop.load(std::memory_order_relaxed)) {
        const int rc = adapter_.Warmup(bar);
        if (rc == NQ_NEXT_EVENT) {
            Discover(bar.instrument);
            on_bar(bar);
            continue;
        }
        if (rc == NQ_NEXT_AGAIN) {
            std::this_thread::yield();
            continue;
        }

        if (rc != NQ_NEXT_END) End(rc);
        return;
    }
}

template <class Record>
void AdapterEngine<Record>::Start() {
    thread_ = std::jthread([this](std::stop_token st) { Run(st); });
}

template <class Record>
void AdapterEngine<Record>::Stop() noexcept {
    if (!thread_.joinable()) return;
    thread_.request_stop();
    thread_.join();
}

template <class Record>
std::optional<Instrument> AdapterEngine<Record>::FindInstrument(std::uint32_t id) const {
    std::lock_guard lock(mu_);
    auto it = instruments_.find(id);
    if (it == instruments_.end()) return std::nullopt;
    return it->second;
}

template <class Record>
void AdapterEngine<Record>::Discover(std::uint32_t id) {
    if (!seen_.insert(id).second) return;
    
    const auto found = adapter_.Instrument(id);
    if (!found) return;
    
    std::lock_guard lock(mu_);
    instruments_.emplace(id, Instrument{id, found->symbol ? found->symbol : "", found->tick_size, found->lot_size});
}

template <class Record>
bool AdapterEngine<Record>::Push(Record record, const std::stop_token& st) noexcept {
    bool stalled = false;
    while (!queue_.try_push(std::move(record))) {
        if (st.stop_requested()) return false;
        
        if (!stalled) {
            stalled = true;
            stalls_.fetch_add(1, std::memory_order_relaxed);
        }
        
        std::this_thread::yield();
    }
    
    return true;
}

template <class Record>
void AdapterEngine<Record>::Run(std::stop_token st) {
    Record record;
    
    while (!st.stop_requested()) {
        const int rc = adapter_.Next(record);
        if (rc == NQ_NEXT_EVENT) {
            Discover(record.instrument);
            if (!Push(record, st)) return;
            continue;
        }
        if (rc == NQ_NEXT_AGAIN) {
            std::this_thread::yield();
            continue;
        }
        
        End(rc);
        return;
    }
}

template <class Record>
void AdapterEngine<Record>::End(int rc) noexcept {
    status_.store(rc == NQ_NEXT_END ? 0 : rc, std::memory_order_relaxed);
    ended_.store(true, std::memory_order_release);
}

template class AdapterEngine<nq_mbo>;
template class AdapterEngine<nq_ohlcv>;

}  // namespace nq
