#include <atomic>
#include <cmath>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <vector>

#include <nq/adapter_host.hpp>
#include <nq/params.hpp>

#include <Pipeline.h>
#include <PipelineTable.h>
#include <Session.h>

#include "support.hpp"

using namespace nqtest;

static std::optional<nq::Instrument> Resolve(std::uint32_t id) {
    if (id == 1) return nq::Instrument{1, "AAA", 10, 1};
    if (id == 2) return nq::Instrument{2, "BBB", 5, 100};
    return std::nullopt;
}

static nq_ohlcv Minute(std::int64_t minute, std::uint32_t instrument) {
    const double base = static_cast<double>(minute);
    return Ohlcv(minute * kMin, instrument, 100 + base, 102 + base, 99 + base, 101 + base, 1 + 0.5 * base);
}

static bool RowIs(const nq::RowBuffer& rows, std::size_t i, std::int64_t ts, double close, double volume, double count) {
    if (i >= rows.RowCount() || rows.Ts(i) != ts) return false;
    const auto r = rows.Row(i);
    return Near(r[0], close) && Near(r[1], 3) && Near(r[2], volume) && Near(r[3], count);
}

static int TestPipeline() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_OHLCV_FEATURES_PATH, "test");
    CHECK(catalog.GetFeed() == nq::Feed::OHLCV && catalog.Columns().size() == 4);
    CHECK(catalog.Columns()[0].name == "test.close" && catalog.Columns()[2].kind == NQ_COLUMN_CUMULATIVE);
    catalog.AddSampler(nq::SamplerSpec::Time("1m", kMin));
    catalog.AddSampler(nq::SamplerSpec::Time("5m", 5 * kMin));
    catalog.AddSampler(nq::SamplerSpec::Events("each", 1));

    nq::Pipeline<nq_ohlcv> pipe(catalog, nq::Instrument{0, "TEST", 1, 1});
    const nq::RowBuffer& one_minute = pipe.Engine().GetSampler(0).Rows();
    const nq::RowBuffer& five_minutes = pipe.Engine().GetSampler(1).Rows();
    const nq::RowBuffer& each = pipe.Engine().GetSampler(2).Rows();

    for (std::int64_t minute = 0; minute < 7; ++minute) {
        pipe.Engine().Advance(minute * kMin);
        pipe.Apply(Minute(minute, 0));
    }
    CHECK(each.RowCount() == 7 && RowIs(each, 0, 0, 101, 1, 1) && RowIs(each, 6, 6 * kMin, 107, 4, 1));
    CHECK(one_minute.RowCount() == 6 && RowIs(one_minute, 0, kMin, 101, 1, 1) && RowIs(one_minute, 5, 6 * kMin, 106, 3.5, 1));
    CHECK(five_minutes.RowCount() == 1 && RowIs(five_minutes, 0, 5 * kMin, 105, 10, 5));

    pipe.Close();
    CHECK(each.RowCount() == 7);
    CHECK(one_minute.RowCount() == 7 && RowIs(one_minute, 6, 7 * kMin, 107, 4, 1));
    CHECK(five_minutes.RowCount() == 2 && RowIs(five_minutes, 1, 10 * kMin, 107, 7.5, 2));
    return 0;
}

static int TestTable() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_OHLCV_FEATURES_PATH, "test");
    catalog.AddSampler(nq::SamplerSpec::Time("1m", kMin));

    nq::PipelineTable<nq_ohlcv> table(catalog, &Resolve);
    table.Apply(Minute(0, 1));
    table.Apply(Minute(0, 2));
    table.Apply(Minute(1, 1));
    table.Apply(Minute(2, 1));
    table.Apply(Minute(2, 3));
    CHECK(table.InstrumentCount() == 3 && table.Stats().events == 5 && table.Stats().ts_regressions == 0);
    CHECK(table.Stats().instrument_unknown == 1 && table.Find(3)->Engine().GetInstrument().symbol == "#3");
    CHECK(table.Find(1)->Engine().GetInstrument().symbol == "AAA" && table.Entries()[1].stats.events == 3);

    const nq::RowBuffer& a = table.Find(1)->Engine().GetSampler(0).Rows();
    const nq::RowBuffer& b = table.Find(2)->Engine().GetSampler(0).Rows();
    CHECK(a.RowCount() == 2 && RowIs(a, 0, kMin, 101, 1, 1) && RowIs(a, 1, 2 * kMin, 102, 1.5, 1));
    CHECK(b.RowCount() == 1 && RowIs(b, 0, kMin, 101, 1, 1));

    table.Close();
    CHECK(a.RowCount() == 3 && RowIs(a, 2, 3 * kMin, 103, 2, 1));
    CHECK(b.RowCount() == 1);
    return 0;
}

static int TestSession() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_OHLCV_FEATURES_PATH, "test");
    catalog.AddSampler(nq::SamplerSpec::Events("each", 1));
    catalog.AddSampler(nq::SamplerSpec::Time("1m", kMin));

    nq::host::AdapterPlugin adapter(NQ_SYNTHETIC_OHLCV_ADAPTER_PATH);
    CHECK(adapter.Schema().feed == NQ_FEED_OHLCV);
    nq::ParamsBuilder pb;
    pb.PriceScale(NQ_PRICE_SCALE).Set("count", "20").Set("instruments", "2");
    nq::Session<nq_ohlcv> session(catalog, adapter, *pb.Build(), nullptr, 8);
    std::atomic<bool> stop{false};
    const nq::RunResult r = session.Run(stop);
    CHECK(!r.interrupted && r.adapter_status == 0 && r.consumed == 20);

    const nq::PipelineTable<nq_ohlcv>& table = session.Pipelines();
    CHECK(table.InstrumentCount() == 2 && table.Stats().events == 20 && table.Stats().instrument_unknown == 0);
    CHECK(table.Find(2)->Engine().GetInstrument().symbol == "SYN2");
    const nq::RowBuffer& each = table.Find(2)->Engine().GetSampler(0).Rows();
    const nq::RowBuffer& one_minute = table.Find(2)->Engine().GetSampler(1).Rows();
    CHECK(each.RowCount() == 10 && RowIs(each, 9, 9 * kMin, 110, 2, 1));
    CHECK(one_minute.RowCount() == 10 && RowIs(one_minute, 9, 10 * kMin, 110, 2, 1));
    return 0;
}

template <class Step>
static bool ThrowsFeedMismatch(Step&& step) {
    try {
        step();
    } catch (const nq::FeedMismatchError&) {
        return true;
    }
    return false;
}

static int TestFeedRules() {
    nq::FeatureCatalog mbo;
    mbo.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "book");
    CHECK(mbo.GetFeed() == nq::Feed::MBO);
    CHECK(ThrowsFeedMismatch([&] { mbo.LoadPlugin(NQ_EXAMPLE_OHLCV_FEATURES_PATH, "ohlcv"); }));
    CHECK(mbo.PluginCount() == 1);
    mbo.AddSampler(nq::SamplerSpec::Trades("t", 1));
    mbo.RequireFeed(nq::Feed::MBO);
    CHECK(ThrowsFeedMismatch([&] { mbo.RequireFeed(nq::Feed::OHLCV); }));

    nq::FeatureCatalog ohlcv;
    ohlcv.LoadPlugin(NQ_EXAMPLE_OHLCV_FEATURES_PATH, "ohlcv");
    CHECK(ThrowsFeedMismatch([&] { ohlcv.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "book"); }));
    CHECK(ThrowsFeedMismatch([&] { ohlcv.AddSampler(nq::SamplerSpec::Trades("t", 1)); }));
    ohlcv.AddSampler(nq::SamplerSpec::Events("e", 1));
    ohlcv.RequireFeed(nq::Feed::OHLCV);
    CHECK(ThrowsFeedMismatch([&] { ohlcv.RequireFeed(nq::Feed::MBO); }));

    nq::FeatureCatalog no_plugins;
    no_plugins.AddSampler(nq::SamplerSpec::Trades("t", 1));
    no_plugins.RequireFeed(nq::Feed::MBO);
    CHECK(ThrowsFeedMismatch([&] { no_plugins.RequireFeed(nq::Feed::OHLCV); }));
    return 0;
}

static int TestTechnicalAnalysis() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_TA_OHLCV_PATH, "ta");
    CHECK(catalog.Columns().size() == 28 && catalog.Columns()[0].name == "ta.ret" && catalog.Columns()[11].name == "ta.rvol");
    CHECK(catalog.Columns()[12].name == "ta.natr_rank" && catalog.Columns()[13].name == "ta.natr_z");
    CHECK(catalog.Columns()[27].name == "ta.rvol_z" && catalog.Columns()[27].kind == NQ_COLUMN_SNAPSHOT);
    catalog.AddSampler(nq::SamplerSpec::Events("each", 1));

    nq::Pipeline<nq_ohlcv> pipe(catalog, nq::Instrument{0, "TEST", 1, 1});
    for (std::int64_t minute = 0; minute < 60; ++minute) {
        pipe.Engine().Advance(minute * kMin);
        pipe.Apply(Minute(minute, 0));
    }
    const nq::RowBuffer& each = pipe.Engine().GetSampler(0).Rows();
    CHECK(each.RowCount() == 60);

    const auto warming = each.Row(12);
    CHECK(std::isnan(warming[4]) && std::isnan(warming[5]) && std::isnan(warming[6]) && std::isnan(warming[7]));
    CHECK(Near(warming[8], 100) && std::isnan(warming[10]) && std::isnan(warming[11]));

    const auto last = each.Row(59);
    const double ret = std::log(160.0 / 159.0);
    const double log_range = std::log(161.0 / 158.0);
    CHECK(Near(last[0], ret) && Near(last[1], 0) && Near(last[2], ret * ret));
    CHECK(Near(last[3], 0.5 * log_range * log_range - (2 * std::numbers::ln2 - 1) * ret * ret));
    CHECK(Near(last[4], 3.0 / 160.0) && Near(last[5], 9.5 / 3.0) && Near(last[6], 100) && Near(last[7], 21.0 / 22.0));
    CHECK(Near(last[8], 100) && Near(last[9], 2.0 / 3.0) && Near(last[10], 1) && Near(last[11], 30.5 / 25.25));

    nq::FeatureCatalog rth_only;
    rth_only.LoadPlugin(NQ_TA_OHLCV_PATH, "ta", {{"rth_only", "1"}});
    rth_only.AddSampler(nq::SamplerSpec::Events("each", 1));
    nq::Pipeline<nq_ohlcv> rth_pipe(rth_only, nq::Instrument{0, "TEST", 1, 1});
    const std::int64_t ten_eastern = 15 * 60 * kMin;
    for (std::int64_t minute = 0; minute < 3; ++minute) rth_pipe.Apply(Minute(minute, 0));
    rth_pipe.Apply(Ohlcv(ten_eastern, 0, 200, 202, 199, 201, 1));
    rth_pipe.Apply(Ohlcv(ten_eastern + kMin, 0, 201, 203, 200, 202, 1));
    const nq::RowBuffer& rth_rows = rth_pipe.Engine().GetSampler(0).Rows();
    CHECK(rth_rows.RowCount() == 5 && Near(rth_rows.Row(2)[0], 0) && std::isnan(rth_rows.Row(2)[9]));
    CHECK(Near(rth_rows.Row(3)[0], 0) && Near(rth_rows.Row(3)[9], 2.0 / 3.0));
    CHECK(Near(rth_rows.Row(4)[0], std::log(202.0 / 201.0)) && Near(rth_rows.Row(4)[1], 0));
    return 0;
}

static std::optional<nq::Instrument> ResolveCross(std::uint32_t id) {
    static const char* const kSymbols[] = {"AAA", "BBB", "CCC", "DDD", "QQQ"};
    if (id < 1 || id > 5) return std::nullopt;
    return nq::Instrument{id, kSymbols[id - 1], 1, 1};
}

static nq_ohlcv ClvBar(std::int64_t ts, std::uint32_t instrument, double close) {
    return Ohlcv(ts, instrument, 100, 110, 90, close, 1);
}

static std::vector<double> ZScores(const std::vector<double>& x) {
    double mean = 0.0;
    for (const double v : x) mean += v / static_cast<double>(x.size());
    double var = 0.0;
    for (const double v : x) var += (v - mean) * (v - mean) / static_cast<double>(x.size());
    std::vector<double> z;
    for (const double v : x) z.push_back((v - mean) / std::sqrt(var));
    return z;
}

static int TestTechnicalCross() {
    constexpr std::int64_t kHour = 60 * kMin;
    constexpr std::size_t kClv = 9, kNatrRank = 12, kNatrZ = 13, kClvRank = 22, kClvZ = 23;

    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_TA_OHLCV_PATH, "ta", {{"cross_min", "3"}});
    catalog.AddSampler(nq::SamplerSpec::Time("1h", kHour));
    catalog.AddCross("xs", "ta", kHour);
    const nq::CrossTemplate& t = catalog.Crosses()[0];
    CHECK(t.names.size() == 24 && t.names[0] == "ta.natr" && t.names[7] == "ta.rvol" && t.names[8] == "xs.natr_rank");

    nq::PipelineTable<nq_ohlcv> table(catalog, &ResolveCross);
    const double hour0[] = {95, 100, 105, 110, 100};
    for (std::uint32_t id = 1; id <= 5; ++id) table.Apply(ClvBar(0, id, hour0[id - 1]));
    table.Apply(ClvBar(kHour, 1, 110));
    table.Apply(ClvBar(kHour, 2, 90));
    table.Apply(ClvBar(kHour, 3, 100));
    table.Apply(ClvBar(2 * kHour, 1, 100));
    table.Apply(ClvBar(2 * kHour, 2, 100));
    CHECK(std::isnan(table.Find(4)->Engine().ReadAll()[kClvRank]));
    // Every instrument gets the same bar, so clv is 5/11 for all of them.
    for (std::uint32_t id = 1; id <= 5; ++id) table.Apply(Ohlcv(3 * kHour, id, 95, 101, 90, 95, 1));
    table.Close();

    const nq::RowBuffer& a = table.Find(1)->Engine().GetSampler(0).Rows();
    const nq::RowBuffer& b = table.Find(2)->Engine().GetSampler(0).Rows();
    const nq::RowBuffer& c = table.Find(3)->Engine().GetSampler(0).Rows();
    const nq::RowBuffer& d = table.Find(4)->Engine().GetSampler(0).Rows();
    const nq::RowBuffer& q = table.Find(5)->Engine().GetSampler(0).Rows();
    CHECK(a.RowCount() == 4 && b.RowCount() == 4 && c.RowCount() == 3 && d.RowCount() == 2 && q.RowCount() == 2);

    // At 1h all five are in the cross-section, QQQ too. BBB and QQQ tie at the average rank.
    const std::vector<double> z1 = ZScores({0.25, 0.5, 0.75, 1.0, 0.5});
    CHECK(a.Ts(0) == kHour && Near(a.At(0, kClv), 0.25) && Near(a.At(0, kClvRank), 0.0) && Near(a.At(0, kClvZ), z1[0]));
    CHECK(Near(b.At(0, kClvRank), 0.375) && Near(b.At(0, kClvZ), z1[1]));
    CHECK(Near(c.At(0, kClvRank), 0.75) && Near(c.At(0, kClvZ), z1[2]));
    CHECK(Near(d.At(0, kClvRank), 1.0) && Near(d.At(0, kClvZ), z1[3]));
    CHECK(Near(q.At(0, kClvRank), 0.375) && Near(q.At(0, kClvZ), z1[4]));
    // natr is NaN while the average true range warms up, so it has no cross-section.
    CHECK(std::isnan(a.At(0, 4)) && std::isnan(a.At(0, kNatrRank)) && std::isnan(a.At(0, kNatrZ)));

    // At 2h only AAA, BBB and CCC have a bar. DDD and QQQ keep their 1h values but stay out.
    const std::vector<double> z2 = ZScores({1.0, 0.0, 0.5});
    CHECK(a.Ts(1) == 2 * kHour && Near(a.At(1, kClvRank), 1.0) && Near(a.At(1, kClvZ), z2[0]));
    CHECK(Near(b.At(1, kClvRank), 0.0) && Near(b.At(1, kClvZ), z2[1]));
    CHECK(Near(c.At(1, kClvRank), 0.5) && Near(c.At(1, kClvZ), z2[2]));
    const auto& blocks = table.Crosses()[0].Blocks();
    CHECK(blocks.size() == 4 && blocks[1].ts == 2 * kHour);
    CHECK(blocks[1].instruments == (std::vector<std::uint32_t>{1, 2, 3}));

    // At 3h only AAA and BBB have a bar, below cross_min 3.
    CHECK(a.Ts(2) == 3 * kHour && std::isnan(a.At(2, kClvRank)) && std::isnan(a.At(2, kClvZ)));
    CHECK(std::isnan(b.At(2, kClvRank)) && Near(b.At(2, kClv), 0.5));

    // At 4h the equal clv values tie at rank 0.5 with z exactly 0, though their mean is off by rounding.
    for (const nq::RowBuffer* rows : {&a, &b, &c, &d, &q}) {
        const std::size_t last = rows->RowCount() - 1;
        CHECK(rows->Ts(last) == 4 * kHour && Near(rows->At(last, kClvRank), 0.5) && rows->At(last, kClvZ) == 0.0);
    }
    return 0;
}

int main() {
    if (int r = TestPipeline()) return r;
    if (int r = TestTable()) return r;
    if (int r = TestSession()) return r;
    if (int r = TestFeedRules()) return r;
    if (int r = TestTechnicalAnalysis()) return r;
    if (int r = TestTechnicalCross()) return r;
    std::printf("ok\n");
    return 0;
}
