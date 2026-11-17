#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
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
    // Also rejects Databento's undefined price, INT64_MAX.
    static constexpr std::int64_t kMaxLakePrice = std::numeric_limits<std::int64_t>::max() / kPriceFactor;

    explicit LakeOhlcv(const nq::Params& p)
        : path_(LakePath()),
          max_jump_(p.GetDouble("max_jump", 50.0)),
          reanchor_bars_(p.GetInt("reanchor_bars", 3)) {
        if (!(max_jump_ > 1.0)) throw std::invalid_argument(std::string(kName) + ": max_jump must be above 1");
        if (reanchor_bars_ < 1) throw std::invalid_argument(std::string(kName) + ": reanchor_bars must be at least 1");
        file_ = Unwrap(arrow::io::MemoryMappedFile::Open(path_, arrow::io::FileMode::READ));
        reader_ = Unwrap(arrow::ipc::RecordBatchFileReader::Open(file_));
        RequireOhlcvSchema(*reader_->schema());
    }

    nq::Next Next(nq::Ohlcv& out) {
        std::int64_t i = 0;
        for (;;) {
            while (row_ == rows_) {
                if (next_batch_ == reader_->num_record_batches()) return End();
                LoadBatch(next_batch_++);
            }
            i = row_++;
            if (!ValidBar(i)) {
                ++skipped_;
            } else if (AcceptJump(i)) {
                break;
            } else {
                ++jumps_;
            }
        }
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
    static bool ValidPrice(std::int64_t price) noexcept { return price > 0 && price <= kMaxLakePrice; }

    bool ValidBar(std::int64_t i) const noexcept {
        const std::int64_t open = open_[i];
        const std::int64_t high = high_[i];
        const std::int64_t low = low_[i];
        const std::int64_t close = close_[i];
        return ValidPrice(open) && ValidPrice(high) && ValidPrice(low) && ValidPrice(close) &&
               low <= std::min(open, close) && std::max(open, close) <= high && volume_[i] >= 0;
    }

    bool Jumped(std::int64_t i, double reference) const noexcept {
        for (const std::int64_t p : {open_[i], high_[i], low_[i], close_[i]}) {
            const double price = static_cast<double>(p);
            if (price > reference * max_jump_ || price * max_jump_ < reference) return true;
        }
        return false;
    }

    // A bar more than max_jump_ times away from the last accepted close is a bad print. A run of
    // reanchor_bars_ such bars that agree with each other is a real move: its last bar is accepted
    // and becomes the new reference, so a true move larger than max_jump_ cannot block the feed.
    bool AcceptJump(std::int64_t i) {
        JumpState& s = jump_[static_cast<std::uint32_t>(instrument_[i])];
        const double close = static_cast<double>(close_[i]);
        if (s.reference == 0.0 || !Jumped(i, s.reference)) {
            s = JumpState{close, 0, 0.0};
            return true;
        }
        s.run = s.run != 0 && !Jumped(i, s.run_close) ? s.run + 1 : 1;
        s.run_close = close;
        if (s.run < reanchor_bars_) return false;
        s = JumpState{close, 0, 0.0};
        return true;
    }

    nq::Next End() {
        if (!reported_ && (skipped_ != 0 || jumps_ != 0)) {
            std::fprintf(stderr, "%s: skipped %lld bars with invalid prices or volume, %lld bars more than %gx from the last close\n",
                         kName, static_cast<long long>(skipped_), static_cast<long long>(jumps_), max_jump_);
            reported_ = true;
        }
        return nq::Next::End;
    }

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
    struct JumpState {
        double reference = 0.0;
        std::int64_t run = 0;
        double run_close = 0.0;
    };
    double max_jump_;
    std::int64_t reanchor_bars_;
    std::unordered_map<std::uint32_t, JumpState> jump_;
    std::int64_t skipped_ = 0;
    std::int64_t jumps_ = 0;
    bool reported_ = false;
    std::unordered_map<std::uint32_t, nq::Instrument> instruments_;
};

}  // namespace nq_lake

NQ_OHLCV_ADAPTER(nq_lake::LakeOhlcv)
