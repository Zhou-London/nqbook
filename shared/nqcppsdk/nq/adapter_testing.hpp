#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>

#include <nq/adapter.hpp>
#include <nq/adapter_abi.h>

namespace nq::testing {

class MockSink {
public:
    MockSink() = default;
    MockSink(const MockSink&) = delete;
    MockSink& operator=(const MockSink&) = delete;

    const nq_sink* Raw() const noexcept { return &sink_; }
    Sink View() const noexcept { return Sink(&sink_); }

    bool WaitEnd(std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        std::unique_lock lock(mu_);
        return cv_.wait_for(lock, timeout, [&] { return ended_; });
    }

    bool Ended() const {
        std::lock_guard lock(mu_);
        return ended_;
    }

    std::optional<int> Status() const {
        std::lock_guard lock(mu_);
        if (!ended_) return std::nullopt;
        return status_;
    }

    std::vector<nq_mbo> Events() const {
        std::lock_guard lock(mu_);
        return events_;
    }

    std::size_t Count() const {
        std::lock_guard lock(mu_);
        return events_.size();
    }

private:
    static void Push(void* ctx, const nq_mbo* m) noexcept {
        auto& self = *static_cast<MockSink*>(ctx);
        std::lock_guard lock(self.mu_);
        self.events_.push_back(*m);
    }

    static void End(void* ctx, int status) noexcept {
        auto& self = *static_cast<MockSink*>(ctx);
        {
            std::lock_guard lock(self.mu_);
            self.ended_ = true;
            self.status_ = status;
        }
        self.cv_.notify_all();
    }

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::vector<nq_mbo> events_;
    bool ended_ = false;
    int status_ = 0;
    nq_sink sink_{this, &MockSink::Push, &MockSink::End};
};

template <AdapterType A>
class AdapterSandbox {
public:
    explicit AdapterSandbox(const nq_params* params) : adapter_(Params(params), sink_.View()) {}

    MockSink& Sink() noexcept { return sink_; }
    A& Adapter() noexcept { return adapter_; }

    void Start() { adapter_.Start(); }
    void Stop() noexcept { adapter_.Stop(); }

    bool RunToEnd(std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        adapter_.Start();
        const bool ended = sink_.WaitEnd(timeout);
        adapter_.Stop();
        return ended;
    }

private:
    MockSink sink_;
    A adapter_;
};

}  // namespace nq::testing
