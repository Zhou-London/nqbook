#include <Session.h>

#include <thread>

namespace nq {

namespace {

std::optional<WriterEngine> MakeWriter(std::unique_ptr<ParquetWriter> writer, std::size_t queue_capacity) {
    if (!writer) return std::nullopt;
    return std::optional<WriterEngine>(std::in_place, std::move(writer), queue_capacity);
}

}  // namespace

template <class Record>
Session<Record>::Session(const FeatureCatalog& catalog, const host::AdapterPlugin& adapter,
                         const nq_params& adapter_params, std::unique_ptr<ParquetWriter> writer,
                         std::size_t queue_capacity, PipelineOptions<Record> options)
    : adapter_(adapter.Create(adapter_params), queue_capacity),
      writer_(MakeWriter(std::move(writer), queue_capacity)),
      table_(
          catalog, 
          [this](std::uint32_t id) { return adapter_.FindInstrument(id); },
          options,
          defaults::kInstrumentSlots, 
          writer_ ? &*writer_ : nullptr) {}

template <class Record>
RunResult Session<Record>::Run(const std::atomic<bool>& stop, std::size_t batch) {
    RunResult result;
    
    if (writer_) writer_->Start();
    adapter_.Start();
    
    nlib::single_queue<Record>& q = adapter_.Queue();
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
    
    table_.Close();
    result.adapter_status = adapter_.Ended() ? adapter_.Status() : 0;
    result.stalls = adapter_.Stalls();
    
    if (writer_) {
        result.has_writer = true;
        writer_->Finish();
        result.rows_written = writer_->Written();
        result.writer_stalls = writer_->Stalls();
        result.writer_error = writer_->Error();
    }
    return result;
}

template class Session<nq_mbo>;
template class Session<nq_ohlcv>;

}  // namespace nq
