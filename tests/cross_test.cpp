#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>

#include <Feature/CrossPanel.h>
#include <PipelineTable.h>
#include <Writer/CsvWriter.h>
#include <Writer/ParquetWriter.h>
#include <Writer/WriterEngine.h>

#include "support.hpp"

namespace fs = std::filesystem;
using namespace nqtest;

static std::optional<nq::Instrument> Resolve(std::uint32_t id) {
    if (id == 1) return nq::Instrument{1, "AAA", 10, 1};
    if (id == 2) return nq::Instrument{2, "BBB", 5, 100};
    return std::nullopt;
}

struct Call {
    std::int64_t ts;
    std::uint32_t nrow;
    std::size_t changed;
};

struct Demean : nq::CrossOperator {
    explicit Demean(std::vector<Call>* calls) : calls_(calls) {}

    void Apply(const nq::CrossView& v) override {
        if (calls_) calls_->push_back(Call{v.Ts(), v.RowCount(), v.Changed().size()});
        const auto in = v.In(0);
        double sum = 0.0;
        double n = 0.0;
        for (std::uint32_t r = 0; r < v.RowCount(); ++r) {
            if (!v.Valid(r) || std::isnan(in[r])) continue;
            sum += in[r];
            n += 1.0;
        }
        const auto demean = v.Out(0);
        const auto count = v.Out(1);
        for (std::uint32_t r = 0; r < v.RowCount(); ++r) {
            demean[r] = in[r] - sum / n;
            count[r] = n;
        }
    }

private:
    std::vector<Call>* calls_;
};

static nq::CrossSpec DemeanSpec(std::string name, bool fill_gaps, std::vector<Call>* calls = nullptr) {
    nq::CrossSpec spec;
    spec.name = std::move(name);
    spec.every = kSec;
    spec.fill_gaps = fill_gaps;
    spec.inputs = {"test.mid"};
    spec.outputs = {"demean", "n"};
    spec.make = [calls] { return std::make_unique<Demean>(calls); };
    return spec;
}

template <class Table>
static void Feed(Table& table) {
    table.Apply(Add(Sec(0.1), 1, 1, NQ_BID, P(100.0), Q(1)));
    table.Apply(Add(Sec(0.2), 1, 2, NQ_ASK, P(102.0), Q(1)));
    table.Apply(Add(Sec(0.3), 2, 1, NQ_BID, P(50.0), Q(1)));
    table.Apply(Add(Sec(0.4), 2, 2, NQ_ASK, P(52.0), Q(1)));
    table.Apply(Add(Sec(0.5), 3, 1, NQ_BID, P(10.0), Q(1)));
    table.Apply(Add(Sec(1.2), 1, 3, NQ_BID, P(101.0), Q(1)));
    table.Apply(Add(Sec(2.5), 2, 3, NQ_BID, P(51.0), Q(1)));
    table.Apply(Add(Sec(4.7), 3, 2, NQ_ASK, P(12.0), Q(1)));
    table.Close();
}

static double At(const nq::FeatureBlock& b, std::size_t column, std::size_t row) {
    return b.values[column * b.instruments.size() + row];
}

static int TestPanel() {
    nq::CrossPanel p(2, 1, 64);
    CHECK(p.Stride() == 64 && p.RowCount() == 0 && p.Changed().empty());
    p.Touch(0);
    p.Touch(0);
    p.Touch(5);
    CHECK(p.RowCount() == 6 && p.Changed().size() == 2 && p.Changed()[0] == 0 && p.Changed()[1] == 5);
    CHECK(!p.Valid(0) && std::isnan(p.In(1)[5]));
    p.In(0)[0] = 1.5;
    p.In(1)[5] = 2.5;
    p.MarkValid(0);
    p.MarkValid(0);
    CHECK(p.Valid(0) && !p.Valid(5) && p.ValidRows().size() == 1);

    p.Touch(200);
    CHECK(p.Stride() == 256 && p.RowCount() == 201 && p.Changed().size() == 3);
    CHECK(p.In(0)[0] == 1.5 && p.In(1)[5] == 2.5 && std::isnan(p.In(0)[200]) && p.Valid(0));
    CHECK(reinterpret_cast<std::uintptr_t>(p.In(1)) % nq::CrossPanel::kAlign == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(p.Out(0)) % nq::CrossPanel::kAlign == 0);

    const nq_cross_panel raw = p.View(7);
    const nq::CrossView v(&raw);
    CHECK(v.Ts() == 7 && v.RowCount() == 201 && raw.stride == 256 && v.Changed().size() == 3 && v.Valid(0) && !v.Valid(200));
    CHECK(v.In(1).data() == p.In(1) && v.Out(0).size() == 201);

    p.ClearChanged();
    CHECK(p.Changed().empty());
    p.Touch(5);
    CHECK(p.Changed().size() == 1 && p.Changed()[0] == 5);
    return 0;
}

static int TestCatalog() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "test");
    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec));

    const auto rejects = [&](nq::CrossSpec spec) {
        try {
            catalog.AddCross(std::move(spec));
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    nq::CrossSpec cumulative = DemeanSpec("a", false);
    cumulative.inputs = {"test.trades"};
    nq::CrossSpec unknown = DemeanSpec("a", false);
    unknown.inputs = {"test.nope"};
    nq::CrossSpec no_operator = DemeanSpec("a", false);
    no_operator.make = nullptr;
    nq::CrossSpec no_inputs = DemeanSpec("a", false);
    no_inputs.inputs.clear();
    nq::CrossSpec no_period = DemeanSpec("a", false);
    no_period.every = 0;
    CHECK(rejects(cumulative) && rejects(unknown) && rejects(no_operator) && rejects(no_inputs) && rejects(no_period));
    CHECK(rejects(DemeanSpec("1s", false)));

    catalog.AddCross(DemeanSpec("x", false));
    CHECK(rejects(DemeanSpec("x", false)));
    CHECK(catalog.Crosses().size() == 1 && catalog.FindCross("x") && !catalog.FindCross("1s"));
    const nq::CrossTemplate& t = catalog.Crosses()[0];
    CHECK(t.names.size() == 3 && t.names[0] == "test.mid" && t.names[1] == "x.demean" && t.names[2] == "x.n");

    bool threw = false;
    try { catalog.AddSampler(nq::SamplerSpec::Time("x", kSec)); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "late"); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);
    return 0;
}

static int TestStage() {
    std::vector<Call> calls;
    std::vector<Call> fill_calls;
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "test");
    catalog.AddCross(DemeanSpec("x", false, &calls));
    catalog.AddCross(DemeanSpec("xf", true, &fill_calls));

    nq::PipelineTable<nq_mbo> table(catalog, &Resolve, {.expected_orders = 16}, 4);
    Feed(table);

    const nq::CrossStage& x = table.Crosses()[0];
    const auto& b = x.Blocks();
    CHECK(x.Fires() == 4 && b.size() == 4 && calls.size() == 4);
    CHECK(calls[0].changed == 3 && calls[1].changed == 1 && calls[2].changed == 1 && calls[3].changed == 1);
    CHECK(calls[0].nrow == 3 && calls[0].ts == kSec);

    CHECK(b[0].ts == kSec && b[0].table == 0 && b[0].instruments == (std::vector<std::uint32_t>{1, 2, 3}));
    CHECK(Near(At(b[0], 0, 0), 101.0) && Near(At(b[0], 0, 1), 51.0) && std::isnan(At(b[0], 0, 2)));
    CHECK(Near(At(b[0], 1, 0), 25.0) && Near(At(b[0], 1, 1), -25.0) && std::isnan(At(b[0], 1, 2)));
    CHECK(Near(At(b[0], 2, 0), 2.0) && Near(At(b[0], 2, 2), 2.0));

    CHECK(b[1].ts == 2 * kSec && b[1].instruments == (std::vector<std::uint32_t>{1}));
    CHECK(Near(At(b[1], 0, 0), 101.5) && Near(At(b[1], 1, 0), 25.25) && Near(At(b[1], 2, 0), 2.0));

    CHECK(b[2].ts == 3 * kSec && b[2].instruments == (std::vector<std::uint32_t>{2}));
    CHECK(Near(At(b[2], 0, 0), 51.5) && Near(At(b[2], 1, 0), -25.0));

    CHECK(b[3].ts == 5 * kSec && b[3].instruments == (std::vector<std::uint32_t>{3}));
    CHECK(Near(At(b[3], 0, 0), 11.0) && Near(At(b[3], 1, 0), 11.0 - 164.0 / 3.0) && Near(At(b[3], 2, 0), 3.0));

    const nq::CrossStage& xf = table.Crosses()[1];
    const auto& f = xf.Blocks();
    CHECK(xf.Fires() == 5 && f.size() == 5 && fill_calls.size() == 5);
    for (std::size_t i = 0; i < f.size(); ++i) {
        CHECK(f[i].ts == static_cast<std::int64_t>(i + 1) * kSec && f[i].table == 1);
        CHECK(f[i].instruments == (std::vector<std::uint32_t>{1, 2, 3}));
    }
    CHECK(fill_calls[2].changed == 1 && fill_calls[3].changed == 0 && fill_calls[4].changed == 1);
    CHECK(f[2].values.size() == 9 && f[3].values.size() == 9);
    for (std::size_t i = 0; i < 9; ++i) {
        CHECK(f[2].values[i] == f[3].values[i] || (std::isnan(f[2].values[i]) && std::isnan(f[3].values[i])));
    }
    CHECK(Near(At(f[1], 0, 1), 51.0) && Near(At(f[1], 1, 1), 51.0 - 76.25) && Near(At(f[2], 0, 1), 51.5));
    CHECK(Near(At(f[4], 0, 2), 11.0) && Near(At(f[4], 2, 0), 3.0));
    return 0;
}

static int TestGrowth() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "test");
    catalog.AddCross(DemeanSpec("x", true));

    const std::uint32_t n = 3000;
    nq::PipelineTable<nq_mbo> table(catalog, nullptr, {.expected_orders = 4}, 4);
    for (std::uint32_t i = 0; i < n; ++i) {
        table.Apply(Add(i + 1, i, 1, NQ_BID, P(10.0 + i), Q(1)));
        table.Apply(Add(i + 1, i, 2, NQ_ASK, P(12.0 + i), Q(1)));
    }
    table.Close();

    const nq::CrossStage& x = table.Crosses()[0];
    CHECK(x.Panel().RowCount() == n && x.Panel().Stride() >= n && x.Blocks().size() == 1);
    const nq::FeatureBlock& b = x.Blocks()[0];
    CHECK(b.ts == kSec && b.instruments.size() == n && b.instruments[0] == 0 && b.instruments[n - 1] == n - 1);
    const double mean = 11.0 + (n - 1) / 2.0;
    CHECK(Near(At(b, 0, 0), 11.0) && Near(At(b, 0, n - 1), 11.0 + (n - 1)));
    CHECK(Near(At(b, 1, 0), 11.0 - mean, 1e-6) && Near(At(b, 2, 17), n));
    return 0;
}

static int TestPluginCross() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_CROSS_FEATURES_PATH, "c");
    catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "plain");
    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec).Columns({"c.mid", "c.market_mid", "c.rel_mid"}));

    const auto rejects = [&](const char* name, const char* plugin, std::int64_t every) {
        try {
            catalog.AddCross(name, plugin, every);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    CHECK(rejects("x", "nope", kSec) && rejects("x", "plain", kSec) && rejects("1s", "c", kSec) && rejects("x", "c", 0));
    catalog.AddCross("x", "c", kSec);
    CHECK(rejects("y", "c", kSec));
    const nq::CrossTemplate& t = catalog.Crosses()[0];
    CHECK(t.plugin == 0 && t.names.size() == 4 && t.names[0] == "c.mid" && t.names[1] == "x.mean" && t.names[3] == "x.rank");

    nq::PipelineTable<nq_mbo> table(catalog, &Resolve, {.expected_orders = 16}, 4);
    Feed(table);

    const auto& b = table.Crosses()[0].Blocks();
    CHECK(b.size() == 4 && b[0].table == 1 && b[0].instruments == (std::vector<std::uint32_t>{1, 2, 3}));
    CHECK(Near(At(b[0], 0, 0), 101.0) && Near(At(b[0], 1, 0), 76.0) && Near(At(b[0], 1, 2), 76.0));
    CHECK(Near(At(b[0], 2, 0), 1.0) && Near(At(b[0], 2, 1), -1.0) && std::isnan(At(b[0], 2, 2)));
    CHECK(At(b[0], 3, 0) == 1.0 && At(b[0], 3, 1) == 0.0 && std::isnan(At(b[0], 3, 2)));
    CHECK(b[1].ts == 2 * kSec && Near(At(b[1], 1, 0), 76.25) && Near(At(b[1], 2, 0), 1.0) && At(b[1], 3, 0) == 1.0);
    CHECK(b[3].ts == 5 * kSec && Near(At(b[3], 0, 0), 11.0) && Near(At(b[3], 1, 0), 164.0 / 3.0) && At(b[3], 3, 0) == 0.0);

    const nq::RowBuffer& a = table.Find(1)->Engine().GetSampler(0).Rows();
    CHECK(a.RowCount() == 2 && a.Ts(0) == kSec && a.Ts(1) == 2 * kSec);
    CHECK(Near(a.At(0, 0), 101.0) && Near(a.At(0, 1), 76.0) && Near(a.At(0, 2), 25.0));
    CHECK(Near(a.At(1, 0), 101.5) && Near(a.At(1, 1), 76.25) && Near(a.At(1, 2), 25.25));
    const nq::RowBuffer& c = table.Find(3)->Engine().GetSampler(0).Rows();
    CHECK(c.RowCount() == 2 && c.Ts(0) == kSec && std::isnan(c.At(0, 0)) && Near(c.At(0, 1), 76.0));
    CHECK(c.Ts(1) == 5 * kSec && Near(c.At(1, 0), 11.0) && Near(c.At(1, 1), 164.0 / 3.0));

    nq::FeatureCatalog wide;
    wide.LoadPlugin(NQ_EXAMPLE_CROSS_FEATURES_PATH, "c");
    wide.AddSampler(nq::SamplerSpec::Time("1s", kSec).Columns({"c.market_mid"}));
    wide.AddCross("x", "c", kSec, true);
    const std::uint32_t n = 3000;
    nq::PipelineTable<nq_mbo> big(wide, nullptr, {.expected_orders = 4}, 4);
    for (std::uint32_t i = 0; i < n; ++i) {
        big.Apply(Add(i + 1, i, 1, NQ_BID, P(10.0 + i), Q(1)));
        big.Apply(Add(i + 1, i, 2, NQ_ASK, P(12.0 + i), Q(1)));
    }
    CHECK(std::isnan(big.Find(0)->Engine().ReadAll()[1]));
    big.Close();
    const double mean = 11.0 + (n - 1) / 2.0;
    CHECK(big.Crosses()[0].Panel().Stride() >= n && big.Crosses()[0].Blocks().size() == 1);
    for (const std::uint32_t id : {0u, 1023u, 1024u, 2999u}) {
        const nq::RowBuffer& rows = big.Find(id)->Engine().GetSampler(0).Rows();
        CHECK(rows.RowCount() == 1 && Near(rows.At(0, 0), mean, 1e-6));
    }
    const nq::FeatureBlock& all = big.Crosses()[0].Blocks()[0];
    CHECK(all.instruments.size() == n && At(all, 3, 0) == 0.0 && At(all, 3, n - 1) == 1.0);
    return 0;
}

static std::shared_ptr<arrow::Table> ReadParquet(const fs::path& path) {
    auto file = arrow::io::ReadableFile::Open(path.string()).ValueOrDie();
    auto reader = parquet::arrow::OpenFile(file, arrow::default_memory_pool()).ValueOrDie();
    return reader->ReadTable().ValueOrDie();
}

static double DoubleAt(const arrow::Table& t, const std::string& column, std::int64_t row) {
    const auto scalar = t.GetColumnByName(column)->GetScalar(row).ValueOrDie();
    return std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value;
}

static int TestOutput(const fs::path& tmp) {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "test");
    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec));
    catalog.AddCross(DemeanSpec("x", false));

    const fs::path dir = tmp / "parquet";
    std::uint64_t written = 0;
    {
        nq::WriterEngine writer(std::make_unique<nq::ParquetWriter>(catalog, dir, nq::ParquetOptions{4}), 64);
        writer.Start();
        nq::PipelineTable<nq_mbo> table(catalog, &Resolve, {.expected_orders = 16}, 4, &writer);
        Feed(table);
        CHECK(table.Crosses()[0].Blocks().empty());
        writer.Finish();
        CHECK(!writer.Failed());
        CHECK(writer.Writer().SamplerCount() == 2 && writer.Writer().Path(1).filename() == "x.parquet");
        CHECK(writer.Writer().Rows(1) == 6);
        written = writer.Written();
    }
    const auto bars = ReadParquet(dir / "1s.parquet");
    const auto x = ReadParquet(dir / "x.parquet");
    CHECK(written == static_cast<std::uint64_t>(bars->num_rows() + x->num_rows()));
    CHECK(x->num_rows() == 6 && x->num_columns() == 7);
    CHECK(x->schema()->field(2)->name() == "ts" && x->schema()->field(2)->type()->ToString() == "int64");
    CHECK(x->schema()->field(3)->name() == "date" && x->schema()->field(3)->type()->ToString() == "date32[day]");
    CHECK(x->schema()->field(4)->name() == "test.mid" && x->schema()->field(6)->name() == "x.n");
    CHECK(Near(DoubleAt(*x, "test.mid", 0), 101.0) && Near(DoubleAt(*x, "x.demean", 1), -25.0));
    CHECK(std::isnan(DoubleAt(*x, "x.demean", 2)) && Near(DoubleAt(*x, "x.demean", 3), 25.25));
    CHECK(Near(DoubleAt(*x, "x.n", 5), 3.0));
    const auto symbol = x->GetColumnByName("symbol")->GetScalar(5).ValueOrDie()->ToString();
    const auto ts = std::static_pointer_cast<arrow::Int64Scalar>(x->GetColumnByName("ts")->GetScalar(5).ValueOrDie());
    const auto day = std::static_pointer_cast<arrow::Date32Scalar>(x->GetColumnByName("date")->GetScalar(5).ValueOrDie());
    CHECK(symbol == "#3" && ts->value == 5 * kSec && day->value == 0);

    nq::ParquetWriter direct(catalog, tmp / "direct");
    direct.Register(nq::Instrument{1, "AAA", 10, 1});
    bool threw = false;
    try { direct.Write(nq::FeatureBlock{1, kSec, {1}, {1.0, 2.0}}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { direct.Write(nq::FeatureBlock{1, kSec, {9}, {1.0, 2.0, 3.0}}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { direct.Write(nq::FeatureBlock{7, kSec, {1}, {1.0, 2.0, 3.0}}); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);
    direct.Write(nq::FeatureBlock{1, kSec, {1}, {1.0, 2.0, 3.0}});
    direct.Write(nq::FeatureBlock{1, 86400 * kSec, {1}, {1.0, 2.0, 3.0}});
    direct.Close();
    CHECK(direct.Rows(1) == 2);
    const auto days = ReadParquet(direct.Path(1))->GetColumnByName("date");
    CHECK(std::static_pointer_cast<arrow::Date32Scalar>(days->GetScalar(0).ValueOrDie())->value == 0);
    CHECK(std::static_pointer_cast<arrow::Date32Scalar>(days->GetScalar(1).ValueOrDie())->value == 1);

    nq::PipelineTable<nq_mbo> table(catalog, &Resolve, {.expected_orders = 16}, 4);
    Feed(table);
    const auto files = nq::WriteSamplerCsv(table, tmp / "csv");
    CHECK(files.size() == 2 && files[1].filename() == "x.csv");
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> csv(std::fopen(files[1].string().c_str(), "r"), &std::fclose);
    CHECK(csv != nullptr);
    char line[256];
    std::vector<std::string> lines;
    while (std::fgets(line, sizeof line, csv.get())) lines.emplace_back(line);
    CHECK(lines.size() == 7 && lines[0] == "instrument,symbol,ts,test.mid,x.demean,x.n\n");
    CHECK(lines[1] == "1,AAA,1000000000,101,25,2\n" && lines[4] == "1,AAA,2000000000,101.5,25.25,2\n");
    return 0;
}

int main() {
    const fs::path tmp = fs::temp_directory_path() / "nqhpc_cross_test";
    fs::remove_all(tmp);
    if (int r = TestPanel()) return r;
    if (int r = TestCatalog()) return r;
    if (int r = TestStage()) return r;
    if (int r = TestGrowth()) return r;
    if (int r = TestPluginCross()) return r;
    if (int r = TestOutput(tmp)) return r;
    fs::remove_all(tmp);
    std::printf("ok\n");
    return 0;
}
