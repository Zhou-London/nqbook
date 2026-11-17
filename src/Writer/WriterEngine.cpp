#include <Writer/WriterEngine.h>

#include <chrono>
#include <exception>
#include <utility>

namespace nq {

namespace {

constexpr int kYieldsBeforeSleep = 64;
constexpr std::chrono::microseconds kIdleSleep{200};

std::deque<nlib::single_queue<FeatureBlock>> SparePools(std::size_t tables, std::size_t capacity) {
    std::deque<nlib::single_queue<FeatureBlock>> pools;
    for (std::size_t t = 0; t < tables; ++t) pools.emplace_back(capacity);
    return pools;
}

}  // namespace

WriterEngine::WriterEngine(std::unique_ptr<ParquetWriter> writer, std::size_t queue_capacity)
    : writer_(std::move(writer)),
      queue_(queue_capacity),
      spare_by_table_(SparePools(writer_->TableCount(), queue_capacity)),
      thread_([this](std::stop_token st) { Run(st); }) {}

void WriterEngine::Register(const Instrument& instrument) { Enqueue(instrument); }

void WriterEngine::Push(FeatureBlock block) { Enqueue(std::move(block)); }

FeatureBlock WriterEngine::Acquire(std::uint32_t table) {
    if (auto block = spare_by_table_[table].try_pop()) return std::move(*block);
    return {};
}

void WriterEngine::Recycle(FeatureBlock block) {
    block.instruments.clear();
    block.values.clear();
    spare_by_table_[block.table].try_push(std::move(block));
}

void WriterEngine::Enqueue(Message message) {
    bool stalled = false;
    while (!queue_.try_push(std::move(message))) {
        if (failed_.load(std::memory_order_acquire)) return;
        if (!stalled) {
            stalled = true;
            stalls_.fetch_add(1, std::memory_order_relaxed);
        }
        std::this_thread::yield();
    }
}

void WriterEngine::Run(std::stop_token st) {
    int empty_polls = 0;
    for (;;) {
        const bool last = st.stop_requested();
        while (auto message = queue_.try_pop()) {
            empty_polls = 0;
            if (failed_.load(std::memory_order_acquire)) continue;
            try {
                if (auto* block = std::get_if<FeatureBlock>(&*message)) {
                    writer_->Write(*block);
                    written_.fetch_add(block->instruments.size(), std::memory_order_relaxed);
                    Recycle(std::move(*block));
                } else {
                    writer_->Register(std::get<Instrument>(*message));
                }
            } catch (const std::exception& e) {
                error_ = e.what();
                failed_.store(true, std::memory_order_release);
            }
        }
        if (last) return;
        if (++empty_polls < kYieldsBeforeSleep) std::this_thread::yield();
        else std::this_thread::sleep_for(kIdleSleep);
    }
}

const std::string& WriterEngine::Finish() {
    thread_.request_stop();
    thread_.join();
    if (!failed_.load(std::memory_order_acquire)) {
        try {
            writer_->Close();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    return error_;
}

}  // namespace nq
