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
    static constexpr std::int64_t kUndefinedLakePrice = std::numeric_limits<std::int64_t>::max();
    static constexpr std::int64_t kMaxLakePrice = kUndefinedLakePrice / kPriceFactor;
    static_assert(kMaxLakePrice < kUndefinedLakePrice);

    explicit LakeOhlcv(const nq::Params& p)
        : path_(LakePath()),
          max_jump_(p.GetDouble("max_jump", 50.0)),
          reanchor_bars_(p.GetInt("reanchor_bars", 3)) {
        if (!(max_jump_ > 1.0)) Fail("max_jump must be above 1");
        if (reanchor_bars_ < 1) Fail("reanchor_bars must be at least 1");
        file_ = Unwrap(arrow::io::MemoryMappedFile::Open(path_, arrow::io::FileMode::READ));
        reader_ = Unwrap(arrow::ipc::RecordBatchFileReader::Open(file_));
        RequireOhlcvSchema(*reader_->schema());
    }

    ~LakeOhlcv() {
        if (skipped_ != 0 || jumps_ != 0) {
            std::fprintf(stderr, "%s: skipped %lld bars with invalid prices or volume, %lld bars more than %gx from the last close\n",
                         kName, static_cast<long long>(skipped_), static_cast<long long>(jumps_), max_jump_);
        }
    }

    nq::Next Next(nq::Ohlcv& out) {
        while (SeekRow()) {
            const std::int64_t i = row_++;
            const LakeBar bar = columns_.Bar(i);
            if (!bar.Valid()) {
                ++skipped_;
                continue;
            }
            const auto id = static_cast<std::uint32_t>(columns_.instrument[i]);
            const auto [it, first_bar] = tracked_.try_emplace(id);
            Tracked& tracked = it->second;
            if (first_bar) {
                tracked = Tracked{nq::Instrument{id, std::string(columns_.symbol->GetView(i))},
                                  JumpFilter{static_cast<double>(bar.close)}};
            } else if (!PassesJumpFilter(tracked.jump, bar)) {
                ++jumps_;
                continue;
            }
            out.ts = columns_.ts[i];
            out.open = bar.open * kPriceFactor;
            out.high = bar.high * kPriceFactor;
            out.low = bar.low * kPriceFactor;
            out.close = bar.close * kPriceFactor;
            out.volume = static_cast<double>(bar.volume);
            out.instrument = id;
            return nq::Next::Event;
        }
        return nq::Next::End;
    }

    const nq::Instrument* FindInstrument(std::uint32_t id) const noexcept {
        const auto it = tracked_.find(id);
        return it == tracked_.end() ? nullptr : &it->second.instrument;
    }

private:
    static bool ValidPrice(std::int64_t price) noexcept { return price > 0 && price <= kMaxLakePrice; }

    struct LakeBar {
        std::int64_t open, high, low, close, volume;

        bool Valid() const noexcept {
            return ValidPrice(open) && ValidPrice(high) && ValidPrice(low) && ValidPrice(close) &&
                   low <= std::min(open, close) && std::max(open, close) <= high && volume >= 0;
        }

        bool FarFrom(double reference, double max_jump) const noexcept {
            for (const std::int64_t p : {open, high, low, close}) {
                const double price = static_cast<double>(p);
                if (price > reference * max_jump || price * max_jump < reference) return true;
            }
            return false;
        }
    };

    struct BarColumns {
        const std::int64_t* ts = nullptr;
        const std::int64_t* instrument = nullptr;
        const std::int64_t* open = nullptr;
        const std::int64_t* high = nullptr;
        const std::int64_t* low = nullptr;
        const std::int64_t* close = nullptr;
        const std::int64_t* volume = nullptr;
        const arrow::StringArray* symbol = nullptr;

        LakeBar Bar(std::int64_t i) const noexcept { return {open[i], high[i], low[i], close[i], volume[i]}; }
    };

    struct Int64Binding {
        const char* name;
        const std::int64_t* BarColumns::* column;
    };
    static constexpr Int64Binding kInt64Columns[] = {
        {"ts_event", &BarColumns::ts}, {"instrument_id", &BarColumns::instrument},
        {"open", &BarColumns::open},   {"high", &BarColumns::high},
        {"low", &BarColumns::low},     {"close", &BarColumns::close},
        {"volume", &BarColumns::volume}};
    static constexpr const char* kSymbolColumn = "symbol";

    struct JumpFilter {
        double anchor_close;
        double far_close = 0.0;
        std::int64_t far_run = 0;
    };

    struct Tracked {
        nq::Instrument instrument;
        JumpFilter jump;
    };

    bool PassesJumpFilter(JumpFilter& filter, const LakeBar& bar) const noexcept {
        const double close = static_cast<double>(bar.close);
        if (bar.FarFrom(filter.anchor_close, max_jump_)) {
            filter.far_run = filter.far_run != 0 && !bar.FarFrom(filter.far_close, max_jump_) ? filter.far_run + 1 : 1;
            filter.far_close = close;
            if (filter.far_run < reanchor_bars_) return false;
        }
        filter = JumpFilter{close};
        return true;
    }

    bool SeekRow() {
        while (row_ == rows_) {
            if (next_batch_ == reader_->num_record_batches()) return false;
            LoadBatch(next_batch_++);
        }
        return true;
    }

    [[noreturn]] static void Fail(const std::string& reason) {
        const std::string message = std::string(kName) + ": " + reason;
        std::fprintf(stderr, "%s\n", message.c_str());
        throw std::runtime_error(message);
    }

    [[noreturn]] void FailFile(const std::string& reason) const { Fail(path_ + ": " + reason); }

    template <class T>
    T Unwrap(arrow::Result<T> result) const {
        if (!result.ok()) FailFile(result.status().ToString());
        return std::move(result).ValueUnsafe();
    }

    static std::string LakePath() {
        const char* home = std::getenv("HOME");
        if (!home) Fail("HOME is not set");
        return std::string(home) + kPathUnderHome;
    }

    void RequireOhlcvSchema(const arrow::Schema& schema) const {
        for (const Int64Binding& binding : kInt64Columns) RequireColumn(schema, binding.name, arrow::Type::INT64);
        RequireColumn(schema, kSymbolColumn, arrow::Type::STRING);
    }

    void RequireColumn(const arrow::Schema& schema, const char* name, arrow::Type::type type) const {
        const auto field = schema.GetFieldByName(name);
        if (!field) FailFile(std::string("not OHLCV data: column ") + name + " is missing");
        if (field->type()->id() != type) {
            FailFile(std::string("not OHLCV data: column ") + name + " has type " + field->type()->ToString());
        }
    }

    void LoadBatch(int index) {
        batch_ = Unwrap(reader_->ReadRecordBatch(index));
        for (const Int64Binding& binding : kInt64Columns) columns_.*binding.column = Int64Column(binding.name);
        columns_.symbol = static_cast<const arrow::StringArray*>(Column(kSymbolColumn));
        rows_ = batch_->num_rows();
        row_ = 0;
    }

    const arrow::Array* Column(const char* name) const {
        const arrow::Array* column = batch_->GetColumnByName(name).get();
        if (column->null_count() != 0) FailFile(std::string("column ") + name + " holds nulls");
        return column;
    }

    const std::int64_t* Int64Column(const char* name) const {
        return static_cast<const arrow::Int64Array*>(Column(name))->raw_values();
    }

    std::string path_;
    std::shared_ptr<arrow::io::MemoryMappedFile> file_;
    std::shared_ptr<arrow::ipc::RecordBatchFileReader> reader_;
    std::shared_ptr<arrow::RecordBatch> batch_;
    BarColumns columns_;
    std::int64_t rows_ = 0;
    std::int64_t row_ = 0;
    int next_batch_ = 0;
    double max_jump_;
    std::int64_t reanchor_bars_;
    std::unordered_map<std::uint32_t, Tracked> tracked_;
    std::int64_t skipped_ = 0;
    std::int64_t jumps_ = 0;
};

}  // namespace nq_lake

NQ_OHLCV_ADAPTER(nq_lake::LakeOhlcv)
