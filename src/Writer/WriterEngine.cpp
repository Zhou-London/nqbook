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

void WriterEngine::Register(const Instrument& instrument) { Enqueue(instrument); }

void WriterEngine::Push(FeatureRow row) {
    if (Enqueue(std::move(row))) pushed_.fetch_add(1, std::memory_order_relaxed);
}

void WriterEngine::Push(FeatureBlock block) {
    const std::uint64_t rows = block.instruments.size();
    if (Enqueue(std::move(block))) pushed_.fetch_add(rows, std::memory_order_relaxed);
}

FeatureBlock WriterEngine::Acquire() {
    if (auto block = spare_.try_pop()) return std::move(*block);
    return {};
}

void WriterEngine::Recycle(FeatureBlock block) {
    block.instruments.clear();
    block.values.clear();
    spare_.try_push(std::move(block));
}

bool WriterEngine::Enqueue(Message message) {
    bool stalled = false;
    while (!queue_.try_push(std::move(message))) {
        if (abort_.load(std::memory_order_acquire) || failed_.load(std::memory_order_acquire)) return false;
        if (!stalled) {
            stalled = true;
            stalls_.fetch_add(1, std::memory_order_relaxed);
        }
        std::this_thread::yield();
    }
    return true;
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
        const bool last = draining_.load(std::memory_order_acquire) || st.stop_requested();
        while (auto message = queue_.try_pop()) {
            idle = 0;
            if (failed_.load(std::memory_order_acquire)) continue;
            try {
                if (const auto* row = std::get_if<FeatureRow>(&*message)) {
                    writer_->Write(*row);
                    written_.fetch_add(1, std::memory_order_relaxed);
                } else if (auto* block = std::get_if<FeatureBlock>(&*message)) {
                    writer_->Write(*block);
                    written_.fetch_add(block->instruments.size(), std::memory_order_relaxed);
                    Recycle(std::move(*block));
                } else {
                    writer_->Register(std::get<Instrument>(*message));
                }
            } catch (const std::exception& e) {
                Fail(e.what());
            }
        }
        if (last) return;
        if (++idle < 64) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

void WriterEngine::Finish() {
    draining_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    if (failed_.load(std::memory_order_acquire)) return;
    try {
        writer_->Close();
    } catch (const std::exception& e) {
        Fail(e.what());
    }
}

}  // namespace nq
