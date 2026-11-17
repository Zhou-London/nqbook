#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include <Feature/FeatureCatalog.h>
#include <Writer/ParquetWriter.h>
#include <Writer/WriterEngine.h>

#include "parquet_support.hpp"
#include "support.hpp"

namespace fs = std::filesystem;
using namespace nqtest;

static nq::FeatureBlock Row(std::uint32_t table, std::uint32_t instrument, std::int64_t ts, std::vector<double> values) {
    return nq::FeatureBlock{table, ts, {instrument}, std::move(values)};
}

static int TestParquetWriter(const nq::FeatureCatalog& catalog, const fs::path& tmp) {
    const fs::path dir = tmp / "direct";
    {
        nq::ParquetWriter w(catalog, dir, nq::ParquetOptions{3});
        CHECK(w.TableCount() == 2 && w.Path(0).filename() == "1s.parquet" && w.Path(1).filename() == "t.parquet");
        w.Register(nq::Instrument{2, "BBB", 5, 100});
        w.Register(nq::Instrument{1, "AAA", 10, 1});
        for (int i = 0; i < 7; ++i) {
            w.Write(Row(0, static_cast<std::uint32_t>(1 + i % 2), kSec * (i + 1), {0.2 + i, 100.0, double(i), 0.0, 1.0}));
        }
        w.Write(Row(1, 1, 86400 * kSec - 1, {1.0}));
        w.Write(Row(1, 2, 86400 * kSec, {2.0}));

        CHECK(Throws<std::invalid_argument>([&] { w.Write(Row(1, 1, 7, {1.0, 2.0})); }));
        CHECK(Throws<std::out_of_range>([&] { w.Write(Row(5, 1, 7, {1.0})); }));
        CHECK(Throws<std::invalid_argument>([&] { w.Write(Row(1, 3, 7, {1.0})); }));
        CHECK(Throws<std::invalid_argument>([&] { w.Register(nq::Instrument{1, "CCC", 1, 1}); }));

        w.Close();
        CHECK(w.Rows(0) == 7 && w.Rows(1) == 2 && w.Files().size() == 3);
        CHECK(Throws<std::logic_error>([&] { w.Write(Row(0, 1, 9, {0, 0, 0, 0, 0})); }));
    }

    const ParquetFile bars = ReadParquet(dir / "1s.parquet");
    CHECK(bars.table->num_rows() == 7 && bars.table->num_columns() == 9 && bars.row_groups == 3);
    CHECK(bars.table->schema()->field(0)->name() == "instrument" && bars.table->schema()->field(1)->name() == "symbol");
    CHECK(bars.table->schema()->field(2)->name() == "ts");
    CHECK(bars.table->schema()->field(3)->name() == "date");
    CHECK(bars.table->schema()->field(4)->name() == "example_features.spread");
    CHECK(bars.table->schema()->field(2)->type()->ToString() == "int64");
    CHECK(bars.table->schema()->field(3)->type()->ToString() == "date32[day]");
    CHECK(ValueAt<arrow::UInt32Scalar>(*bars.table, "instrument", 0) == 1 && ValueAt<arrow::UInt32Scalar>(*bars.table, "instrument", 1) == 2);
    CHECK(StringAt(*bars.table, "symbol", 0) == "AAA" && StringAt(*bars.table, "symbol", 1) == "BBB");
    CHECK(ValueAt<arrow::Int64Scalar>(*bars.table, "ts", 2) == 3 * kSec && ValueAt<arrow::Date32Scalar>(*bars.table, "date", 2) == 0);
    CHECK(Near(ValueAt<arrow::DoubleScalar>(*bars.table, "example_features.spread", 6), 6.2));
    CHECK(Near(ValueAt<arrow::DoubleScalar>(*bars.table, "example_features.buy_vol", 3), 3.0));

    const ParquetFile trades = ReadParquet(dir / "t.parquet");
    CHECK(trades.table->num_rows() == 2 && trades.table->num_columns() == 5 && trades.row_groups == 1);
    CHECK(trades.table->schema()->field(4)->name() == "example_features.trades");
    CHECK(StringAt(*trades.table, "symbol", 1) == "BBB");
    CHECK(ValueAt<arrow::Int64Scalar>(*trades.table, "ts", 0) == 86400 * kSec - 1 && ValueAt<arrow::Date32Scalar>(*trades.table, "date", 0) == 0);
    CHECK(ValueAt<arrow::Int64Scalar>(*trades.table, "ts", 1) == 86400 * kSec && ValueAt<arrow::Date32Scalar>(*trades.table, "date", 1) == 1);
    CHECK(Near(ValueAt<arrow::DoubleScalar>(*trades.table, "example_features.trades", 1), 2.0));

    const ParquetFile inst = ReadParquet(dir / "instruments.parquet");
    CHECK(inst.table->num_rows() == 2 && inst.table->num_columns() == 4);
    CHECK(ValueAt<arrow::UInt32Scalar>(*inst.table, "instrument", 0) == 1 && StringAt(*inst.table, "symbol", 0) == "AAA");
    CHECK(StringAt(*inst.table, "symbol", 1) == "BBB" && ValueAt<arrow::Int64Scalar>(*inst.table, "lot_size", 1) == 100);

    CHECK(Throws<std::exception>([&] {
        std::ofstream(tmp / "not_a_dir") << "x";
        nq::ParquetWriter bad(catalog, tmp / "not_a_dir" / "sub");
    }));
    CHECK(Throws<std::invalid_argument>([&] { nq::ParquetWriter bad(catalog, tmp / "zero", nq::ParquetOptions{0}); }));
    return 0;
}

static int TestWriterEngine(const nq::FeatureCatalog& catalog, const fs::path& tmp) {
    const fs::path dir = tmp / "engine";
    {
        nq::WriterEngine engine(std::make_unique<nq::ParquetWriter>(catalog, dir, nq::ParquetOptions{100}), 4);
        engine.Register(nq::Instrument{1, "AAA", 10, 1});
        engine.Register(nq::Instrument{2, "BBB", 5, 100});
        for (int i = 0; i < 1000; ++i) {
            engine.Push(Row(0, 1, kSec * i, {1.0, 2.0, double(i), 0.0, 0.0}));
            if (i % 10 == 0) engine.Push(Row(1, 2, kSec * i, {double(i)}));
        }
        CHECK(engine.Finish().empty());
        CHECK(engine.Written() == 1100);
        CHECK(engine.Writer().Rows(0) == 1000 && engine.Writer().Rows(1) == 100 && engine.Writer().Closed());
    }
    const ParquetFile bars = ReadParquet(dir / "1s.parquet");
    CHECK(bars.table->num_rows() == 1000 && bars.row_groups == 10);
    CHECK(Near(ValueAt<arrow::DoubleScalar>(*bars.table, "example_features.buy_vol", 999), 999.0));
    CHECK(StringAt(*bars.table, "symbol", 999) == "AAA");
    CHECK(ReadParquet(dir / "t.parquet").table->num_rows() == 100);
    CHECK(ReadParquet(dir / "instruments.parquet").table->num_rows() == 2);
    return 0;
}

static int TestWriterEngineLastRow(const nq::FeatureCatalog& catalog, const fs::path& tmp) {
    const fs::path dir = tmp / "last_row";
    for (int i = 0; i < 2000; ++i) {
        nq::WriterEngine engine(std::make_unique<nq::ParquetWriter>(catalog, dir, nq::ParquetOptions{100}), 4);
        for (int spin = 0; spin < i % 64; ++spin) std::this_thread::yield();
        engine.Register(nq::Instrument{1, "AAA", 10, 1});
        engine.Push(Row(0, 1, kSec, {1.0, 2.0, 3.0, 0.0, 0.0}));
        engine.Finish();
        CHECK(engine.Written() == 1);
    }
    return 0;
}

int main() {
    const fs::path tmp = fs::temp_directory_path() / "nqhpc_writer_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH);
    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec));
    catalog.AddSampler(nq::SamplerSpec::Trades("t", 1).Columns({"example_features.trades"}));

    if (int r = TestParquetWriter(catalog, tmp)) return r;
    if (int r = TestWriterEngine(catalog, tmp)) return r;
    if (int r = TestWriterEngineLastRow(catalog, tmp)) return r;
    fs::remove_all(tmp);
    std::printf("ok\n");
    return 0;
}
