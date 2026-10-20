#include <Writer/WriterEngine.h>

#include <chrono>
#include <exception>
#include <utility>

namespace nq {

WriterEngine::WriterEngine(std::unique_ptr<ParquetWriter> writer, std::size_t queue_capacity)
    : writer_(std::move(writer)), queue_(queue_capacity) {}

WriterEngine::~WriterEngine() {
    Abort();
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void WriterEngine::Start() {
    if (thread_.joinable()) return;
    thread_ = std::jthread([this](std::stop_token st) { Run(st); });
}

void WriterEngine::Push(FeatureRow row) {
    bool stalled = false;
    while (!queue_.try_push(std::move(row))) {
        if (abort_.load(std::memory_order_acquire) || failed_.load(std::memory_order_acquire)) return;
        if (!stalled) {
            stalled = true;
            stalls_.fetch_add(1, std::memory_order_relaxed);
        }
        std::this_thread::yield();
    }
    pushed_.fetch_add(1, std::memory_order_relaxed);
}

void WriterEngine::Abort() noexcept { abort_.store(true, std::memory_order_release); }

void WriterEngine::Fail(const std::string& message) {
    {
        std::lock_guard lock(mu_);
        if (error_.empty()) error_ = message;
    }
    failed_.store(true, std::memory_order_release);
}

std::string WriterEngine::Error() const {
    std::lock_guard lock(mu_);
    return error_;
}

void WriterEngine::Run(std::stop_token st) {
    int idle = 0;
    for (;;) {
        auto row = queue_.try_pop();
        if (row) {
            idle = 0;
            if (!failed_.load(std::memory_order_acquire)) {
                try {
                    writer_->Write(*row);
                    written_.fetch_add(1, std::memory_order_relaxed);
                } catch (const std::exception& e) {
                    Fail(e.what());
                }
            }
            continue;
        }
        if (draining_.load(std::memory_order_acquire) || st.stop_requested()) return;
        if (++idle < 64) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

void WriterEngine::Finish(std::span<const Instrument> instruments) {
    draining_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    if (failed_.load(std::memory_order_acquire)) return;
    try {
        writer_->WriteInstruments(instruments);
        writer_->Close();
    } catch (const std::exception& e) {
        Fail(e.what());
    }
}

}  // namespace nq
