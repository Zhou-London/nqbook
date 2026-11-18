#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <arrow/ipc/writer.h>

#include <nq/adapter_host.hpp>
#include <nq/params.hpp>

#include "support.hpp"

using namespace nqtest;
namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kLakeScale = 1'000'000'000;
constexpr std::int64_t kUndefPrice = std::numeric_limits<std::int64_t>::max();

struct Bar {
    std::int64_t ts, open, high, low, close, volume;
    std::int64_t instrument = 7;
};

std::shared_ptr<arrow::Array> Int64s(const std::vector<std::int64_t>& values) {
    arrow::Int64Builder b;
    if (!b.AppendValues(values).ok()) std::abort();
    return b.Finish().ValueOrDie();
}

void WriteLake(const fs::path& file, const std::vector<Bar>& bars) {
    std::vector<std::int64_t> ts, id, open, high, low, close, volume;
    arrow::StringBuilder symbol;
    for (const Bar& bar : bars) {
        ts.push_back(bar.ts);
        id.push_back(bar.instrument);
        open.push_back(bar.open);
        high.push_back(bar.high);
        low.push_back(bar.low);
        close.push_back(bar.close);
        volume.push_back(bar.volume);
        if (!symbol.Append("SYM" + std::to_string(bar.instrument)).ok()) std::abort();
    }
    const auto schema = arrow::schema({
        arrow::field("ts_event", arrow::int64()), arrow::field("instrument_id", arrow::int64()),
        arrow::field("open", arrow::int64()), arrow::field("high", arrow::int64()),
        arrow::field("low", arrow::int64()), arrow::field("close", arrow::int64()),
        arrow::field("volume", arrow::int64()), arrow::field("symbol", arrow::utf8()),
    });
    const auto batch = arrow::RecordBatch::Make(
        schema, static_cast<std::int64_t>(bars.size()),
        {Int64s(ts), Int64s(id), Int64s(open), Int64s(high), Int64s(low), Int64s(close), Int64s(volume),
         symbol.Finish().ValueOrDie()});
    fs::create_directories(file.parent_path());
    const auto out = arrow::io::FileOutputStream::Open(file.string()).ValueOrDie();
    const auto writer = arrow::ipc::MakeFileWriter(out, schema).ValueOrDie();
    if (!writer->WriteRecordBatch(*batch).ok() || !writer->Close().ok() || !out->Close().ok()) std::abort();
}

std::int64_t L(double price) { return Scaled(price, kLakeScale); }

}  // namespace

struct Emitted {
    std::vector<nq_ohlcv> warmup;
    std::vector<nq_ohlcv> live;
};

static std::optional<Emitted> RunAdapter(const std::vector<Bar>& lake_bars, nq::ParamsBuilder pb = {}) {
    const fs::path home = fs::temp_directory_path() / ("nqlake_ohlcv_test_" + std::to_string(::getpid()));
    WriteLake(home / ".nqlake" / "mmap.arrow", lake_bars);
    ::setenv("HOME", home.c_str(), 1);
    nq::host::AdapterPlugin plugin(NQ_LAKE_OHLCV_ADAPTER_PATH);
    std::optional<Emitted> got;
    try {
        auto adapter = plugin.Create(*pb.Build());
        got.emplace();
        nq_ohlcv bar{};
        while (adapter.Warmup(bar) == NQ_NEXT_EVENT) got->warmup.push_back(bar);
        while (adapter.Next(bar) == NQ_NEXT_EVENT) got->live.push_back(bar);
    } catch (const nq::host::PluginInitError&) {
    }
    fs::remove_all(home);
    return got;
}

static std::vector<nq_ohlcv> BarsEmittedByAdapter(const std::vector<Bar>& lake_bars, nq::ParamsBuilder pb = {}) {
    return RunAdapter(lake_bars, std::move(pb)).value().live;
}

static Bar Flat(std::int64_t ts, double price, std::int64_t instrument = 7) {
    return {ts, L(price), L(price), L(price), L(price), 10, instrument};
}

static std::vector<std::int64_t> Times(const std::vector<nq_ohlcv>& bars) {
    std::vector<std::int64_t> ts;
    for (const nq_ohlcv& bar : bars) ts.push_back(bar.ts);
    return ts;
}

static int TestSkipsInvalidBars() {
    const auto got = BarsEmittedByAdapter({
        {1, L(10), L(11), L(9), L(10.5), 100},
        {2, kUndefPrice, kUndefPrice, kUndefPrice, kUndefPrice, 1},
        {3, L(10), L(11), L(9), -L(1), 5},
        {4, L(10), L(9), L(11), L(10), 5},
        {5, L(12), L(11), L(9), L(10), 5},
        {6, L(10), L(11), L(9), L(10), -1},
        {7, L(10.5), L(12), L(10), L(11), 200},
    });
    CHECK(got.size() == 2);
    CHECK(got[0].ts == 1 && got[0].close == P(10.5) && got[0].instrument == 7 && Near(got[0].volume, 100));
    CHECK(got[1].ts == 7 && got[1].open == P(10.5) && got[1].high == P(12) && got[1].low == P(10));
    return 0;
}

static int TestDropsBadPrintAndKeepsLastAcceptedClose() {
    const auto got = BarsEmittedByAdapter({Flat(1, 334.94), Flat(2, 4.26), Flat(3, 333.83), Flat(4, 327.76)});
    CHECK((Times(got) == std::vector<std::int64_t>{1, 3, 4}));
    return 0;
}

static int TestDropsBarWhoseHighAloneIsFar() {
    const auto got = BarsEmittedByAdapter({Flat(1, 100), {2, L(100), L(6000), L(99), L(100), 10}, Flat(3, 101)});
    CHECK((Times(got) == std::vector<std::int64_t>{1, 3}));
    return 0;
}

static int TestKeepsSplitsWithinDefaultMaxJump() {
    const auto got = BarsEmittedByAdapter({Flat(1, 5000), Flat(2, 200), Flat(3, 200), Flat(4, 4), Flat(5, 4.1)});
    CHECK((Times(got) == std::vector<std::int64_t>{1, 2, 3, 4, 5}));
    return 0;
}

static int TestMaxJumpParamLowersLimit() {
    nq::ParamsBuilder strict;
    strict.Set("max_jump", "10");
    const auto got = BarsEmittedByAdapter({Flat(1, 5000), Flat(2, 200), Flat(3, 199), Flat(4, 198), Flat(5, 197)}, strict);
    CHECK((Times(got) == std::vector<std::int64_t>{1, 4, 5}));
    return 0;
}

static int TestReanchorsOnThirdAgreeingFarBar() {
    const auto got = BarsEmittedByAdapter({Flat(1, 100), Flat(2, 1), Flat(3, 1.01), Flat(4, 0.99), Flat(5, 1)});
    CHECK((Times(got) == std::vector<std::int64_t>{1, 4, 5}));
    CHECK(got[1].close == P(0.99));
    return 0;
}

static int TestDisagreeingFarBarsRestartRun() {
    const auto got = BarsEmittedByAdapter({Flat(1, 100), Flat(2, 1), Flat(3, 9000), Flat(4, 1), Flat(5, 101), Flat(6, 1),
                               Flat(7, 1), Flat(8, 1)});
    CHECK((Times(got) == std::vector<std::int64_t>{1, 5, 8}));
    return 0;
}

static int TestInstrumentsAreIndependent() {
    const auto got = BarsEmittedByAdapter({Flat(1, 100, 7), Flat(2, 1, 8), Flat(3, 101, 7), Flat(4, 1.01, 8)});
    CHECK((Times(got) == std::vector<std::int64_t>{1, 2, 3, 4}));
    return 0;
}

static int TestWarmupUntilSplitsBarsAtThatDay() {
    constexpr std::int64_t kSecondDay = 24 * 3600 * kSec;
    nq::ParamsBuilder from_second_day;
    from_second_day.Set("warmup_until", "1970-01-02");
    const auto got = RunAdapter({Flat(1, 100), Flat(2, 1), Flat(kSecondDay - 1, 101), Flat(kSecondDay, 102),
                                 Flat(kSecondDay + 1, 103)},
                                from_second_day);
    CHECK((Times(got->warmup) == std::vector<std::int64_t>{1, kSecondDay - 1}));
    CHECK((Times(got->live) == std::vector<std::int64_t>{kSecondDay, kSecondDay + 1}));

    const auto no_warmup = RunAdapter({Flat(1, 100), Flat(kSecondDay, 102)});
    CHECK(no_warmup->warmup.empty() && no_warmup->live.size() == 2);

    for (const char* bad : {"1970-13-01", "19700102", "1970-01-02T00", ""}) {
        nq::ParamsBuilder rejected;
        rejected.Set("warmup_until", bad);
        CHECK(!RunAdapter({Flat(1, 100)}, rejected));
    }
    return 0;
}

int main() {
    if (int r = TestWarmupUntilSplitsBarsAtThatDay()) return r;
    if (int r = TestSkipsInvalidBars()) return r;
    if (int r = TestDropsBadPrintAndKeepsLastAcceptedClose()) return r;
    if (int r = TestDropsBarWhoseHighAloneIsFar()) return r;
    if (int r = TestKeepsSplitsWithinDefaultMaxJump()) return r;
    if (int r = TestMaxJumpParamLowersLimit()) return r;
    if (int r = TestReanchorsOnThirdAgreeingFarBar()) return r;
    if (int r = TestDisagreeingFarBarsRestartRun()) return r;
    if (int r = TestInstrumentsAreIndependent()) return r;
    std::printf("ok\n");
    return 0;
}
