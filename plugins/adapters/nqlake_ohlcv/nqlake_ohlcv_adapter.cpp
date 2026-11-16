#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <arrow/ipc/reader.h>

#include <nq/adapter.hpp>

namespace nq_lake {

class LakeOhlcv : public nq::AdapterBase {
public:
    static constexpr const char* kName = "nqlake_ohlcv";
    static constexpr const char* kPathUnderHome = "/.nqlake/mmap.arrow";
    static constexpr std::int64_t kLakePriceScale = 1'000'000'000;
    static constexpr std::int64_t kPriceFactor = NQ_PRICE_SCALE / kLakePriceScale;
    static_assert(NQ_PRICE_SCALE % kLakePriceScale == 0);

    explicit LakeOhlcv(const nq::Params&) : path_(LakePath()) {
        file_ = Unwrap(arrow::io::MemoryMappedFile::Open(path_, arrow::io::FileMode::READ));
        reader_ = Unwrap(arrow::ipc::RecordBatchFileReader::Open(file_));
        RequireOhlcvSchema(*reader_->schema());
    }

    nq::Next Next(nq::Ohlcv& out) {
        while (row_ == rows_) {
            if (next_batch_ == reader_->num_record_batches()) return nq::Next::End;
            LoadBatch(next_batch_++);
        }
        const std::int64_t i = row_++;
        const auto id = static_cast<std::uint32_t>(instrument_[i]);
        if (!instruments_.contains(id)) {
            instruments_.emplace(id, nq::Instrument{id, std::string(symbol_->GetView(i))});
        }
        out.ts = ts_[i];
        out.open = open_[i] * kPriceFactor;
        out.high = high_[i] * kPriceFactor;
        out.low = low_[i] * kPriceFactor;
        out.close = close_[i] * kPriceFactor;
        out.volume = static_cast<double>(volume_[i]);
        out.instrument = id;
        return nq::Next::Event;
    }

    const nq::Instrument* FindInstrument(std::uint32_t id) const noexcept {
        const auto it = instruments_.find(id);
        return it == instruments_.end() ? nullptr : &it->second;
    }

private:
    [[noreturn]] void Fail(const std::string& reason) const {
        const std::string message = std::string(kName) + ": " + path_ + ": " + reason;
        std::fprintf(stderr, "%s\n", message.c_str());
        throw std::runtime_error(message);
    }

    template <class T>
    T Unwrap(arrow::Result<T> result) const {
        if (!result.ok()) Fail(result.status().ToString());
        return std::move(result).ValueUnsafe();
    }

    static std::string LakePath() {
        const char* home = std::getenv("HOME");
        if (!home) throw std::runtime_error(std::string(kName) + ": HOME is not set");
        return std::string(home) + kPathUnderHome;
    }

    void RequireOhlcvSchema(const arrow::Schema& schema) const {
        for (const char* name : {"ts_event", "instrument_id", "open", "high", "low", "close", "volume"}) {
            RequireColumn(schema, name, arrow::Type::INT64);
        }
        RequireColumn(schema, "symbol", arrow::Type::STRING);
    }

    void RequireColumn(const arrow::Schema& schema, const char* name, arrow::Type::type type) const {
        const auto field = schema.GetFieldByName(name);
        if (!field) Fail(std::string("not OHLCV data: column ") + name + " is missing");
        if (field->type()->id() != type) {
            Fail(std::string("not OHLCV data: column ") + name + " has type " + field->type()->ToString());
        }
    }

    void LoadBatch(int index) {
        batch_ = Unwrap(reader_->ReadRecordBatch(index));
        ts_ = Int64Column("ts_event");
        instrument_ = Int64Column("instrument_id");
        open_ = Int64Column("open");
        high_ = Int64Column("high");
        low_ = Int64Column("low");
        close_ = Int64Column("close");
        volume_ = Int64Column("volume");
        symbol_ = static_cast<const arrow::StringArray*>(Column("symbol"));
        rows_ = batch_->num_rows();
        row_ = 0;
    }

    const arrow::Array* Column(const char* name) const {
        const arrow::Array* column = batch_->GetColumnByName(name).get();
        if (column->null_count() != 0) Fail(std::string("column ") + name + " holds nulls");
        return column;
    }

    const std::int64_t* Int64Column(const char* name) const {
        return static_cast<const arrow::Int64Array*>(Column(name))->raw_values();
    }

    std::string path_;
    std::shared_ptr<arrow::io::MemoryMappedFile> file_;
    std::shared_ptr<arrow::ipc::RecordBatchFileReader> reader_;
    std::shared_ptr<arrow::RecordBatch> batch_;
    const std::int64_t* ts_ = nullptr;
    const std::int64_t* instrument_ = nullptr;
    const std::int64_t* open_ = nullptr;
    const std::int64_t* high_ = nullptr;
    const std::int64_t* low_ = nullptr;
    const std::int64_t* close_ = nullptr;
    const std::int64_t* volume_ = nullptr;
    const arrow::StringArray* symbol_ = nullptr;
    std::int64_t rows_ = 0;
    std::int64_t row_ = 0;
    int next_batch_ = 0;
    std::unordered_map<std::uint32_t, nq::Instrument> instruments_;
};

}  // namespace nq_lake

NQ_OHLCV_ADAPTER(nq_lake::LakeOhlcv)
