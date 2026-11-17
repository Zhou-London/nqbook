#include <Writer/ParquetWriter.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/writer.h>
#include <parquet/properties.h>

namespace nq {

namespace {

void Check(const arrow::Status& status, const std::filesystem::path& path) {
    if (!status.ok()) throw std::runtime_error(path.string() + ": " + status.ToString());
}

template <typename T>
T Unwrap(arrow::Result<T> result, const std::filesystem::path& path) {
    Check(result.status(), path);
    return std::move(result).ValueOrDie();
}

std::shared_ptr<parquet::WriterProperties> WriterProperties(std::int64_t row_group_rows) {
    return parquet::WriterProperties::Builder()
        .compression(arrow::Compression::ZSTD)
        ->max_row_group_length(row_group_rows)
        ->version(parquet::ParquetVersion::PARQUET_2_6)
        ->data_page_version(parquet::ParquetDataPageVersion::V2)
        ->build();
}

std::shared_ptr<parquet::ArrowWriterProperties> ArrowProperties() {
    return parquet::ArrowWriterProperties::Builder().store_schema()->build();
}

std::int32_t DayOf(std::int64_t ns) {
    using namespace std::chrono;
    return static_cast<std::int32_t>(floor<days>(nanoseconds{ns}).count());
}

std::unique_ptr<parquet::arrow::FileWriter> OpenWriter(const std::filesystem::path& path,
                                                       const arrow::Schema& schema,
                                                       std::int64_t row_group_rows) {
    auto sink = Unwrap(arrow::io::FileOutputStream::Open(path.string()), path);
    return Unwrap(parquet::arrow::FileWriter::Open(schema, arrow::default_memory_pool(), std::move(sink),
                                                   WriterProperties(row_group_rows), ArrowProperties()),
                  path);
}

}  // namespace

struct ParquetWriter::Table {
    std::filesystem::path path;
    std::shared_ptr<arrow::Schema> schema;
    std::unique_ptr<parquet::arrow::FileWriter> writer;
    arrow::UInt32Builder instrument;
    arrow::StringBuilder symbol;
    arrow::Int64Builder ts;
    arrow::Date32Builder date;
    std::vector<arrow::TypedBufferBuilder<double>> columns;
    std::int64_t pending = 0;
    std::uint64_t rows = 0;

    Table(std::filesystem::path p, std::shared_ptr<arrow::Schema> s, std::size_t ncol,
          std::int64_t row_group_rows)
        : path(std::move(p)),
          schema(std::move(s)),
          writer(OpenWriter(path, *schema, row_group_rows)),
          instrument(arrow::default_memory_pool()),
          ts(arrow::default_memory_pool()),
          date(arrow::default_memory_pool()) {
        columns.resize(ncol);
    }
};

ParquetWriter::ParquetWriter(const FeatureCatalog& catalog, const std::filesystem::path& dir, ParquetOptions options)
    : options_(options), instruments_path_(dir / "instruments.parquet") {
    if (options_.row_group_rows <= 0) throw std::invalid_argument("row_group_rows must be positive");
    std::filesystem::create_directories(dir);
    for (std::size_t t = 0; t < catalog.TableCount(); ++t) {
        const TableSchema table = catalog.Table(t);
        arrow::FieldVector fields;
        fields.push_back(arrow::field("instrument", arrow::uint32(), false));
        fields.push_back(arrow::field("symbol", arrow::utf8(), false));
        fields.push_back(arrow::field("ts", arrow::int64(), false));
        fields.push_back(arrow::field("date", arrow::date32(), false));
        for (const auto& name : table.columns) fields.push_back(arrow::field(name, arrow::float64(), false));
        tables_.push_back(std::make_unique<Table>(dir / (table.name + ".parquet"), arrow::schema(std::move(fields)),
                                                  table.columns.size(), options_.row_group_rows));
    }
}

ParquetWriter::~ParquetWriter() {
    try {
        Close();
    } catch (...) {
    }
}

const std::filesystem::path& ParquetWriter::Path(std::size_t table) const noexcept { return tables_[table]->path; }

std::uint64_t ParquetWriter::Rows(std::size_t table) const noexcept { return tables_[table]->rows; }

std::vector<std::filesystem::path> ParquetWriter::Files() const {
    std::vector<std::filesystem::path> out;
    for (const auto& t : tables_) out.push_back(t->path);
    if (instruments_written_) out.push_back(instruments_path_);
    return out;
}

void ParquetWriter::Register(const Instrument& instrument) {
    if (closed_) throw std::logic_error("ParquetWriter::Register after Close");
    if (!instruments_.emplace(instrument.id, instrument).second) {
        throw std::invalid_argument("instrument " + std::to_string(instrument.id) + " is registered twice");
    }
}

void ParquetWriter::Write(const FeatureBlock& block) {
    if (closed_) throw std::logic_error("ParquetWriter::Write after Close");
    if (block.table >= tables_.size()) throw std::out_of_range("FeatureBlock.table out of range");
    Table& t = *tables_[block.table];
    const std::size_t nrow = block.instruments.size();
    if (block.values.size() != nrow * t.columns.size()) {
        throw std::invalid_argument(t.path.string() + ": block has " + std::to_string(block.values.size()) +
                                    " values, schema needs " + std::to_string(nrow * t.columns.size()));
    }
    for (const std::uint32_t id : block.instruments) {
        if (!instruments_.contains(id)) {
            throw std::invalid_argument(t.path.string() + ": instrument " + std::to_string(id) + " is not registered");
        }
    }
    const auto n = static_cast<std::int64_t>(nrow);
    Check(t.instrument.AppendValues(block.instruments.data(), n), t.path);
    for (const std::uint32_t id : block.instruments) Check(t.symbol.Append(instruments_.find(id)->second.symbol), t.path);
    Check(t.ts.Reserve(n), t.path);
    for (std::int64_t i = 0; i < n; ++i) t.ts.UnsafeAppend(block.ts);
    const std::int32_t day = DayOf(block.ts);
    Check(t.date.Reserve(n), t.path);
    for (std::int64_t i = 0; i < n; ++i) t.date.UnsafeAppend(day);
    for (std::size_t c = 0; c < t.columns.size(); ++c) {
        Check(t.columns[c].Append(block.values.data() + c * nrow, n), t.path);
    }
    t.pending += n;
    if (t.pending >= options_.row_group_rows) Flush(t);
}

void ParquetWriter::Flush(Table& t) {
    if (t.pending == 0) return;
    arrow::ArrayVector arrays;
    arrays.reserve(4 + t.columns.size());
    arrays.push_back(Unwrap(t.instrument.Finish(), t.path));
    arrays.push_back(Unwrap(t.symbol.Finish(), t.path));
    arrays.push_back(Unwrap(t.ts.Finish(), t.path));
    arrays.push_back(Unwrap(t.date.Finish(), t.path));
    for (auto& c : t.columns) arrays.push_back(std::make_shared<arrow::DoubleArray>(t.pending, Unwrap(c.Finish(), t.path)));
    const auto batch = arrow::RecordBatch::Make(t.schema, t.pending, std::move(arrays));
    Check(t.writer->WriteRecordBatch(*batch), t.path);
    t.rows += static_cast<std::uint64_t>(t.pending);
    t.pending = 0;
}

void ParquetWriter::WriteInstruments() {
    std::vector<const Instrument*> by_id;
    by_id.reserve(instruments_.size());
    for (const auto& [id, instrument] : instruments_) by_id.push_back(&instrument);
    std::ranges::sort(by_id, {}, &Instrument::id);

    const auto schema = arrow::schema({
        arrow::field("instrument", arrow::uint32(), false),
        arrow::field("symbol", arrow::utf8(), false),
        arrow::field("tick_size", arrow::int64(), false),
        arrow::field("lot_size", arrow::int64(), false),
    });
    arrow::UInt32Builder ids;
    arrow::StringBuilder symbols;
    arrow::Int64Builder ticks;
    arrow::Int64Builder lots;
    for (const Instrument* i : by_id) {
        Check(ids.Append(i->id), instruments_path_);
        Check(symbols.Append(i->symbol), instruments_path_);
        Check(ticks.Append(i->tick_size), instruments_path_);
        Check(lots.Append(i->lot_size), instruments_path_);
    }
    arrow::ArrayVector arrays;
    arrays.push_back(Unwrap(ids.Finish(), instruments_path_));
    arrays.push_back(Unwrap(symbols.Finish(), instruments_path_));
    arrays.push_back(Unwrap(ticks.Finish(), instruments_path_));
    arrays.push_back(Unwrap(lots.Finish(), instruments_path_));
    const auto batch = arrow::RecordBatch::Make(schema, static_cast<std::int64_t>(by_id.size()), std::move(arrays));

    auto writer = OpenWriter(instruments_path_, *schema, options_.row_group_rows);
    Check(writer->WriteRecordBatch(*batch), instruments_path_);
    Check(writer->Close(), instruments_path_);
    instruments_written_ = true;
}

void ParquetWriter::Close() {
    if (closed_) return;
    closed_ = true;
    for (auto& t : tables_) {
        Flush(*t);
        Check(t->writer->Close(), t->path);
    }
    WriteInstruments();
}

}  // namespace nq
