#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>

#include <Feature/FeatureCatalog.h>
#include <Writer/ParquetWriter.h>
#include <Writer/WriterEngine.h>

#include "support.hpp"

namespace fs = std::filesystem;
using namespace nqtest;

struct Parquet {
    std::shared_ptr<arrow::Table> table;
    int row_groups = 0;
};

static Parquet Read(const fs::path& path) {
    auto file = arrow::io::ReadableFile::Open(path.string()).ValueOrDie();
    auto reader = parquet::arrow::OpenFile(file, arrow::default_memory_pool()).ValueOrDie();
    Parquet out;
    out.row_groups = reader->parquet_reader()->metadata()->num_row_groups();
    out.table = reader->ReadTable().ValueOrDie();
    return out;
}

static double DoubleAt(const arrow::Table& t, const std::string& column, std::int64_t row) {
    const auto chunked = t.GetColumnByName(column);
    const auto scalar = chunked->GetScalar(row).ValueOrDie();
    return std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value;
}

static std::int64_t Int64At(const arrow::Table& t, const std::string& column, std::int64_t row) {
    const auto scalar = t.GetColumnByName(column)->GetScalar(row).ValueOrDie();
    if (scalar->type->id() == arrow::Type::TIMESTAMP) return std::static_pointer_cast<arrow::TimestampScalar>(scalar)->value;
    if (scalar->type->id() == arrow::Type::UINT32) return std::static_pointer_cast<arrow::UInt32Scalar>(scalar)->value;
    return std::static_pointer_cast<arrow::Int64Scalar>(scalar)->value;
}

static std::string StringAt(const arrow::Table& t, const std::string& column, std::int64_t row) {
    const auto scalar = t.GetColumnByName(column)->GetScalar(row).ValueOrDie();
    return std::static_pointer_cast<arrow::StringScalar>(scalar)->ToString();
}

static nq::FeatureRow Row(std::uint32_t sampler, std::uint32_t instrument, std::int64_t ts, std::vector<double> values) {
    return nq::FeatureRow{sampler, instrument, ts, std::move(values)};
}

static int TestParquetWriter(const nq::FeatureCatalog& catalog, const fs::path& tmp) {
    const fs::path dir = tmp / "direct";
    {
        nq::ParquetWriter w(catalog, dir, nq::ParquetOptions{3});
        CHECK(w.SamplerCount() == 2 && w.Path(0).filename() == "1s.parquet" && w.Path(1).filename() == "t.parquet");
        for (int i = 0; i < 7; ++i) {
            w.Write(Row(0, static_cast<std::uint32_t>(1 + i % 2), kSec * (i + 1), {0.2 + i, 100.0, double(i), 0.0, 1.0}));
        }
        w.Write(Row(1, 1, 5, {1.0}));
        w.Write(Row(1, 2, 6, {2.0}));

        bool threw = false;
        try { w.Write(Row(1, 1, 7, {1.0, 2.0})); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { w.Write(Row(5, 1, 7, {1.0})); } catch (const std::out_of_range&) { threw = true; }
        CHECK(threw);

        const std::vector<nq::Instrument> instruments{{1, "AAA", 10, 1}, {2, "BBB", 5, 100}};
        w.WriteInstruments(instruments);
        w.Close();
        CHECK(w.Rows(0) == 7 && w.Rows(1) == 2 && w.Files().size() == 3);
        threw = false;
        try { w.Write(Row(0, 1, 9, {0, 0, 0, 0, 0})); } catch (const std::logic_error&) { threw = true; }
        CHECK(threw);
    }

    const Parquet bars = Read(dir / "1s.parquet");
    CHECK(bars.table->num_rows() == 7 && bars.table->num_columns() == 7 && bars.row_groups == 3);
    CHECK(bars.table->schema()->field(0)->name() == "instrument" && bars.table->schema()->field(1)->name() == "ts");
    CHECK(bars.table->schema()->field(2)->name() == "example_features.spread");
    CHECK(bars.table->schema()->field(1)->type()->ToString() == "timestamp[ns, tz=UTC]");
    CHECK(Int64At(*bars.table, "instrument", 0) == 1 && Int64At(*bars.table, "instrument", 1) == 2);
    CHECK(Int64At(*bars.table, "ts", 2) == 3 * kSec);
    CHECK(Near(DoubleAt(*bars.table, "example_features.spread", 6), 6.2));
    CHECK(Near(DoubleAt(*bars.table, "example_features.buy_vol", 3), 3.0));

    const Parquet trades = Read(dir / "t.parquet");
    CHECK(trades.table->num_rows() == 2 && trades.table->num_columns() == 3 && trades.row_groups == 1);
    CHECK(trades.table->schema()->field(2)->name() == "example_features.trades");
    CHECK(Near(DoubleAt(*trades.table, "example_features.trades", 1), 2.0));

    const Parquet inst = Read(dir / "instruments.parquet");
    CHECK(inst.table->num_rows() == 2 && inst.table->num_columns() == 4);
    CHECK(StringAt(*inst.table, "symbol", 1) == "BBB" && Int64At(*inst.table, "lot_size", 1) == 100);

    bool threw = false;
    try {
        std::ofstream(tmp / "not_a_dir") << "x";
        nq::ParquetWriter bad(catalog, tmp / "not_a_dir" / "sub");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try { nq::ParquetWriter bad(catalog, tmp / "zero", nq::ParquetOptions{0}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    return 0;
}

static int TestWriterEngine(const nq::FeatureCatalog& catalog, const fs::path& tmp) {
    const fs::path dir = tmp / "engine";
    {
        nq::WriterEngine engine(std::make_unique<nq::ParquetWriter>(catalog, dir, nq::ParquetOptions{100}), 4);
        engine.Start();
        for (int i = 0; i < 1000; ++i) {
            engine.Push(Row(0, 1, kSec * i, {1.0, 2.0, double(i), 0.0, 0.0}));
            if (i % 10 == 0) engine.Push(Row(1, 2, kSec * i, {double(i)}));
        }
        const std::vector<nq::Instrument> instruments{{1, "AAA", 10, 1}, {2, "BBB", 5, 100}};
        engine.Finish(instruments);
        CHECK(!engine.Failed() && engine.Error().empty());
        CHECK(engine.Pushed() == 1100 && engine.Written() == 1100);
        CHECK(engine.Writer().Rows(0) == 1000 && engine.Writer().Rows(1) == 100 && engine.Writer().Closed());
    }
    const Parquet bars = Read(dir / "1s.parquet");
    CHECK(bars.table->num_rows() == 1000 && bars.row_groups == 10);
    CHECK(Near(DoubleAt(*bars.table, "example_features.buy_vol", 999), 999.0));
    CHECK(Read(dir / "t.parquet").table->num_rows() == 100);
    CHECK(Read(dir / "instruments.parquet").table->num_rows() == 2);
    return 0;
}

int main() {
    const fs::path tmp = fs::temp_directory_path() / "nqbook_writer_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH);
    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec));
    catalog.AddSampler(nq::SamplerSpec::Trades("t", 1).Columns({"example_features.trades"}));

    if (int r = TestParquetWriter(catalog, tmp)) return r;
    if (int r = TestWriterEngine(catalog, tmp)) return r;
    fs::remove_all(tmp);
    std::printf("ok\n");
    return 0;
}
