#include <Adapter/AdapterEngine.h>

#include <utility>

namespace nq {

AdapterEngine::AdapterEngine(host::AdapterInstance adapter, std::size_t queue_capacity)
    : adapter_(std::move(adapter)), queue_(queue_capacity) {}

AdapterEngine::~AdapterEngine() { Stop(); }

void AdapterEngine::Start() {
    if (thread_.joinable()) return;
    thread_ = std::jthread([this](std::stop_token st) { Run(st); });
}

void AdapterEngine::Stop() noexcept {
    if (!thread_.joinable()) return;
    thread_.request_stop();
    thread_.join();
}

std::optional<Instrument> AdapterEngine::FindInstrument(std::uint32_t id) const {
    std::lock_guard lock(mu_);
    auto it = instruments_.find(id);
    if (it == instruments_.end()) return std::nullopt;
    return it->second;
}

void AdapterEngine::Discover(std::uint32_t id) {
    if (!seen_.insert(id).second) return;
    std::optional<Instrument> found;
    if (auto i = adapter_.Instrument(id)) {
        found = Instrument{id, i->symbol ? i->symbol : "", i->tick_size, i->lot_size};
    }
    std::lock_guard lock(mu_);
    instruments_.emplace(id, std::move(found));
}

bool AdapterEngine::Push(nq_mbo m, const std::stop_token& st) noexcept {
    bool stalled = false;
    while (!queue_.try_push(std::move(m))) {
        if (st.stop_requested()) return false;
        if (!stalled) {
            stalled = true;
            stalls_.fetch_add(1, std::memory_order_relaxed);
        }
        std::this_thread::yield();
    }
    pushed_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void AdapterEngine::Run(std::stop_token st) {
    nq_mbo m;
    while (!st.stop_requested()) {
        const int rc = adapter_.Next(m);
        if (rc == NQ_NEXT_EVENT) {
            Discover(m.instrument);
            if (!Push(m, st)) return;
            continue;
        }
        if (rc == NQ_NEXT_AGAIN) {
            std::this_thread::yield();
            continue;
        }
        status_.store(rc == NQ_NEXT_END ? 0 : rc, std::memory_order_relaxed);
        ended_.store(true, std::memory_order_release);
        return;
    }
}

}  // namespace nq
