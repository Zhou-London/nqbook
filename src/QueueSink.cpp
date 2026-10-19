#include <QueueSink.h>

#include <cstddef>
#include <cstring>
#include <thread>

namespace nq {

static_assert(sizeof(nq_mbo) == sizeof(nlib::mbo));
static_assert(offsetof(nq_mbo, ts) == offsetof(nlib::mbo, ts));
static_assert(offsetof(nq_mbo, id) == offsetof(nlib::mbo, id));
static_assert(offsetof(nq_mbo, price) == offsetof(nlib::mbo, price));
static_assert(offsetof(nq_mbo, qty) == offsetof(nlib::mbo, qty));
static_assert(offsetof(nq_mbo, instrument) == offsetof(nlib::mbo, instrument));
static_assert(offsetof(nq_mbo, side) == offsetof(nlib::mbo, side));
static_assert(offsetof(nq_mbo, action) == offsetof(nlib::mbo, action));

QueueSink::QueueSink(std::size_t capacity)
    : queue_(capacity), sink_{this, &QueueSink::Push, &QueueSink::End} {}

void QueueSink::Push(void* ctx, const nq_mbo* m) noexcept {
    auto& self = *static_cast<QueueSink*>(ctx);
    nlib::mbo out;
    std::memcpy(&out, m, sizeof(out));
    bool stalled = false;
    while (!self.queue_.try_push(std::move(out))) {
        if (self.abort_.load(std::memory_order_acquire)) return;
        if (!stalled) {
            stalled = true;
            self.stalls_.fetch_add(1, std::memory_order_relaxed);
        }
        std::this_thread::yield();
    }
    self.pushed_.fetch_add(1, std::memory_order_relaxed);
}

void QueueSink::End(void* ctx, int status) noexcept {
    auto& self = *static_cast<QueueSink*>(ctx);
    self.status_.store(status, std::memory_order_relaxed);
    self.ended_.store(true, std::memory_order_release);
}

}  // namespace nq
