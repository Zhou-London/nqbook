#include <array>
#include <atomic>
#include <cmath>

#include <nq/adapter_host.hpp>

#include <KeyValues.h>
#include <PipelineTable.h>
#include <Session.h>

#include "support.hpp"

using namespace nqtest;

struct HistoryRow {
    double bars, close, volume;
};

static bool RowIs(const nq::RowBuffer& rows, std::size_t i, std::int64_t ts, const HistoryRow& want) {
    return RowIs(rows, i, ts, std::array{want.bars, want.close, want.volume});
}

static int TestWarmupReachesFeaturesWithoutSampling() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_WARMUP_FEATURES_PATH, "w");
    catalog.AddSampler(nq::SamplerSpec::Events("each", 1));
    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec, true));

    nq::PipelineTable<nq_mbo> table(catalog, ResolveTwoInstruments);
    table.Warmup(Ohlcv(Sec(-7200), 1, 100, 102, 99, 101, 5));
    table.Warmup(Ohlcv(Sec(-7200), 2, 50, 51, 49, 50.5, 8));
    table.Warmup(Ohlcv(Sec(-3600), 1, 101, 104, 100, 103, 7));
    table.FinishWarmup();

    CHECK(table.Stats().warmup_bars == 3 && table.Stats().events == 0 && table.InstrumentCount() == 2);
    const nq::RowBuffer& each = table.Find(1)->Engine().GetSampler(0).Rows();
    const nq::RowBuffer& seconds = table.Find(1)->Engine().GetSampler(1).Rows();
    CHECK(each.Empty() && seconds.Empty());

    table.Apply(Add(Sec(0.1), 1, 1, NQ_ASK, P(100.7), Q(3)));
    table.Apply(Trade(Sec(0.5), 1, 1, NQ_ASK, P(100.7), Q(2)));
    table.Apply(Add(Sec(1.2), 2, 2, NQ_BID, P(50), Q(1)));
    table.Close();

    CHECK(each.RowCount() == 2);
    CHECK(RowIs(each, 0, Sec(0.1), {.bars = 2, .close = 103, .volume = 0}));
    CHECK(RowIs(each, 1, Sec(0.5), {.bars = 2, .close = 103, .volume = 2}));
    CHECK(seconds.RowCount() == 2 && RowIs(seconds, 0, Sec(1), {.bars = 2, .close = 103, .volume = 2}));
    CHECK(RowIs(table.Find(2)->Engine().GetSampler(0).Rows(), 0, Sec(1.2), {.bars = 1, .close = 50.5, .volume = 0}));
    CHECK(table.Stats().events == 3 && table.Stats().first_ts == Sec(0.1));
    return 0;
}

static nq_ohlcv WavyMinute(std::int64_t minute) {
    const double mid = 100 + 5 * std::sin(0.7 * static_cast<double>(minute));
    const double close = mid + std::cos(1.3 * static_cast<double>(minute));
    return Ohlcv(minute * kMin, 1, mid, mid + 2, mid - 2, close, 10 + static_cast<double>(minute % 7));
}

static int TestWarmedTechnicalAnalysisMatchesFullRun() {
    constexpr std::int64_t kBars = 90;
    constexpr std::int64_t kWarmupBars = 60;

    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_TA_OHLCV_PATH, "ta");
    catalog.AddSampler(nq::SamplerSpec::Events("each", 1));

    nq::PipelineTable<nq_ohlcv> full(catalog, ResolveTwoInstruments);
    nq::PipelineTable<nq_ohlcv> warmed(catalog, ResolveTwoInstruments);
    for (std::int64_t minute = 0; minute < kBars; ++minute) {
        const nq_ohlcv bar = WavyMinute(minute);
        full.Apply(bar);
        if (minute < kWarmupBars) warmed.Warmup(bar);
        if (minute == kWarmupBars) warmed.FinishWarmup();
        if (minute >= kWarmupBars) warmed.Apply(bar);
    }

    const nq::RowBuffer& full_rows = full.Find(1)->Engine().GetSampler(0).Rows();
    const nq::RowBuffer& warmed_rows = warmed.Find(1)->Engine().GetSampler(0).Rows();
    CHECK(full_rows.RowCount() == kBars && warmed_rows.RowCount() == kBars - kWarmupBars);
    for (std::size_t i = 0; i < warmed_rows.RowCount(); ++i) {
        const auto want = full_rows.Row(kWarmupBars + i);
        const auto got = warmed_rows.Row(i);
        CHECK(warmed_rows.Ts(i) == full_rows.Ts(kWarmupBars + i));
        for (std::size_t c = 0; c < want.size(); ++c) CHECK(NearOrBothNan(got[c], want[c]));
    }
    CHECK(std::isfinite(warmed_rows.Row(0)[6]) && std::isnan(full_rows.Row(0)[6]));
    return 0;
}

static int TestSessionWarmsUpBeforeTheFeed() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_WARMUP_FEATURES_PATH, "w");
    catalog.AddSampler(nq::SamplerSpec::Events("each", 1));

    nq::host::AdapterPlugin adapter(NQ_SYNTHETIC_ADAPTER_PATH);
    nq::ParamsBuilder pb = nq::ScaledParams({{"count", "4"}, {"instruments", "2"}, {"warmup", "3"}});
    nq::Session<nq_mbo> session(catalog, adapter, *pb.Build(), nullptr, 8);
    std::atomic<bool> stop{false};
    const nq::RunResult result = session.Run(stop);

    CHECK(result.consumed == 4 && result.adapter_status == 0 && !result.interrupted);
    CHECK(session.Pipelines().Stats().warmup_bars == 3 && session.Pipelines().Stats().events == 4);
    const nq::RowBuffer& first = session.Pipelines().Find(1)->Engine().GetSampler(0).Rows();
    const nq::RowBuffer& second = session.Pipelines().Find(2)->Engine().GetSampler(0).Rows();
    CHECK(first.RowCount() == 2 && RowIs(first, 0, 1, {.bars = 2, .close = 103, .volume = 0}));
    CHECK(second.RowCount() == 2 && RowIs(second, 0, 2, {.bars = 1, .close = 102, .volume = 0}));
    return 0;
}

static int TestWarmupErrorEndsTheRun() {
    nq::FeatureCatalog catalog;
    nq::host::AdapterPlugin adapter(NQ_FLAKY_WARMUP_ADAPTER_PATH);
    nq::ParamsBuilder pb;
    nq::Session<nq_mbo> session(catalog, adapter, *pb.Build(), nullptr, 8);
    std::atomic<bool> stop{false};
    const nq::RunResult result = session.Run(stop);

    CHECK(result.adapter_status == NQ_NEXT_ERROR && result.consumed == 0 && !result.interrupted);
    CHECK(session.Pipelines().Stats().warmup_bars == 1);
    CHECK(session.Pipelines().Find(1)->Engine().GetInstrument().symbol == "FLAKY");
    return 0;
}

static int TestStopInterruptsStalledWarmup() {
    nq::FeatureCatalog catalog;
    nq::host::AdapterPlugin adapter(NQ_FLAKY_WARMUP_ADAPTER_PATH);
    nq::ParamsBuilder pb;
    pb.Set("stall", "true");
    nq::Session<nq_mbo> session(catalog, adapter, *pb.Build(), nullptr, 8);
    std::atomic<bool> stop{true};
    const nq::RunResult result = session.Run(stop);

    CHECK(result.consumed == 0 && session.Pipelines().Stats().warmup_bars == 0);
    return 0;
}

int main() {
    if (int r = TestWarmupReachesFeaturesWithoutSampling()) return r;
    if (int r = TestWarmedTechnicalAnalysisMatchesFullRun()) return r;
    if (int r = TestSessionWarmsUpBeforeTheFeed()) return r;
    if (int r = TestWarmupErrorEndsTheRun()) return r;
    if (int r = TestStopInterruptsStalledWarmup()) return r;
    std::printf("ok\n");
    return 0;
}
