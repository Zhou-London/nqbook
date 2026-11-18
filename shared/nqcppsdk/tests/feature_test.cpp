#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <nq/feature_host.hpp>
#include <nq/feature_testing.hpp>

#include "test_features.hpp"

using nq::Side;
using nq::testing::Add;
using nq::testing::MakeOhlcv;
using nq::testing::Trade;

#include "check.hpp"

static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static int TestSandbox() {
    nq::ParamsBuilder pb;
    pb.Symbol("TEST").PriceScale(100).QtyScale(10).Set("min_qty", "1").Set("min_qty", "5");
    {
        const nq::Params view(pb.Build());
        CHECK(view.Raw()->nkv == 1 && view.GetInt("min_qty", 0) == 5 && !view.Get("absent"));
    }

    nq::testing::Sandbox<nq_test::Spread, nq_test::TradeVolume> sb(pb.Build());
    CHECK(decltype(sb)::BundleType::kNcol == 5);
    CHECK(std::strcmp(decltype(sb)::BundleType::kColumns[2].name, "buy_vol") == 0);
    CHECK(decltype(sb)::BundleType::kRequires == NQ_REQUIRES_MBO);
    CHECK(decltype(sb)::BundleType::kFeed == nq::Feed::MBO);

    auto v = sb.Read();
    CHECK(std::isnan(v[0]) && std::isnan(v[1]) && v[2] == 0 && v[3] == 0 && v[4] == 0);

    sb.Book().SetBids({{10050, 70, 2}, {10040, 10, 1}}).SetAsks({{10070, 30, 1}}).SetTs(7);
    v = sb.Read();
    CHECK(Near(v[0], 0.20) && Near(v[1], 100.60));
    CHECK(sb.Book().View().GetLevel(Side::Bid, 1)->price == 10040);
    CHECK(!sb.Book().View().GetLevel(Side::Bid, 2));
    CHECK(sb.Book().View().Ts() == 7);

    sb.Feed(Add(1, 1, Side::Bid, 10050, 70));
    sb.Feed(Trade(2, 9, Side::Ask, 10070, 30, 30));
    sb.Feed(Trade(3, 1, Side::Bid, 10050, 50, 50));
    sb.Feed(Trade(4, 1, Side::Bid, 10050, 3, 3));
    v = sb.Read();
    CHECK(Near(v[2], 3.0) && Near(v[3], 5.0) && v[4] == 2);

    sb.Control(nq::ControlType::Reset, 5);
    v = sb.Read();
    CHECK(v[2] == 0 && v[3] == 0 && v[4] == 0);
    return 0;
}

static int TestWarmupSandbox() {
    nq::ParamsBuilder pb;
    pb.PriceScale(100);
    nq::testing::Sandbox<nq_test::TradeVolume, nq_test::PriorClose> sb(pb.Build());
    auto v = sb.Read();
    CHECK(std::isnan(v[3]) && v[4] == 0);

    const nq_ohlcv days[] = {MakeOhlcv(-2, 10000, 10250, 9900, 10100, 1.5), MakeOhlcv(-1, 10100, 10300, 10000, 10200, 2.0)};
    sb.Warmup(days);
    sb.Feed(Trade(1, 9, Side::Ask, 10070, 30, 30));
    v = sb.Read();
    CHECK(v[2] == 1 && Near(v[3], 102.0) && v[4] == 2);

    sb.Control(nq::ControlType::Reset, 2);
    v = sb.Read();
    CHECK(v[2] == 0 && Near(v[3], 102.0) && v[4] == 2);

    nq::testing::Sandbox<nq_test::Candle> bars(pb.Build());
    bars.Warmup(days[0]);
    CHECK(std::isnan(bars.Read()[0]));
    return 0;
}

static int TestPlugin() {
    nq::host::Plugin plugin(NQ_TEST_PLUGIN_PATH);
    const nq_schema& s = plugin.Schema();
    CHECK(s.abi_hash == NQ_ABI_HASH);
    CHECK(std::strcmp(s.sdk_version, NQ_SDK_VERSION) == 0);
    CHECK(s.requirement == NQ_REQUIRES_MBO && s.feed == NQ_FEED_MBO);
    CHECK(plugin.Fns().on_event != nullptr && plugin.Fns().on_ohlcv == nullptr);
    CHECK(plugin.Fns().on_warmup != nullptr);
    CHECK(s.ncol == 7);
    CHECK(std::strcmp(plugin.Columns()[0].name, "spread") == 0);
    CHECK(std::strcmp(plugin.Columns()[4].name, "trades") == 0);
    CHECK(std::strcmp(plugin.Columns()[4].unit, "count") == 0);
    CHECK(plugin.Columns()[0].kind == NQ_COLUMN_SNAPSHOT);
    CHECK(plugin.Columns()[4].kind == NQ_COLUMN_CUMULATIVE);

    nq::ParamsBuilder pb;
    pb.Symbol("TEST").PriceScale(100).QtyScale(10);
    nq::host::PluginInstance inst = plugin.Create(*pb.Build());
    CHECK(inst.Ncol() == 7);

    nq::testing::MockBook book;
    book.SetBids({{10050, 70, 2}}).SetAsks({{10070, 30, 1}});

    std::vector<double> out(7);
    inst.Read(out, book.Raw());
    CHECK(Near(out[0], 0.20) && Near(out[1], 100.60) && out[4] == 0 && std::isnan(out[5]) && out[6] == 0);

    inst.OnWarmup(MakeOhlcv(-1, 10100, 10300, 10000, 10200, 2.0));
    inst.Read(out, book.Raw());
    CHECK(Near(out[5], 102.0) && out[6] == 1 && out[4] == 0);

    inst.OnEvent(Trade(2, 9, Side::Ask, 10070, 30, 30), *book.Raw());
    inst.OnEvent(Trade(3, 1, Side::Bid, 10050, 3, 3), *book.Raw());
    inst.Read(out, book.Raw());
    CHECK(Near(out[2], 3.0) && Near(out[3], 0.3) && out[4] == 2);

    inst.OnControl(nq_control{NQ_CONTROL_RESET, 4});
    inst.Read(out, book.Raw());
    CHECK(out[2] == 0 && out[3] == 0 && out[4] == 0);

    std::vector<double> small(4);
    bool threw = false;
    try { inst.Read(small, book.Raw()); } catch (const std::length_error&) { threw = true; }
    CHECK(threw);

    nq::ParamsBuilder bad;
    bad.Symbol("TEST").Set("min_qty", "abc");
    threw = false;
    try { plugin.Create(*bad.Build()); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { nq::host::Plugin missing("/nonexistent/plugin.so"); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    return 0;
}

static_assert(nq::FeatureType<nq_test::Spread> && !nq::OhlcvFeatureType<nq_test::Spread>);
static_assert(nq::OhlcvFeatureType<nq_test::Candle> && !nq::FeatureType<nq_test::Candle>);

static int TestOhlcvSandbox() {
    nq::ParamsBuilder pb;
    pb.PriceScale(100);
    nq::testing::Sandbox<nq_test::Candle, nq_test::Volume> sb(pb.Build());
    CHECK(decltype(sb)::BundleType::kFeed == nq::Feed::OHLCV);
    CHECK(decltype(sb)::BundleType::kNcol == 4);
    CHECK(decltype(sb)::BundleType::kRequires == NQ_REQUIRES_NONE);

    auto v = sb.Read();
    CHECK(std::isnan(v[0]) && std::isnan(v[1]) && v[2] == 0 && v[3] == 0);

    sb.Feed(MakeOhlcv(60, 10000, 10250, 9900, 10100, 1.5));
    sb.Feed(MakeOhlcv(120, 10100, 10300, 10000, 10200, 2.25));
    v = sb.Read();
    CHECK(Near(v[0], 102.0) && Near(v[1], 3.0) && Near(v[2], 3.75) && v[3] == 2);

    sb.Control(nq::ControlType::Reset, 130);
    v = sb.Read();
    CHECK(Near(v[0], 102.0) && v[2] == 0 && v[3] == 0);
    return 0;
}

static int TestOhlcvPlugin() {
    nq::host::Plugin plugin(NQ_TEST_OHLCV_PLUGIN_PATH);
    const nq_schema& s = plugin.Schema();
    CHECK(s.abi_hash == NQ_ABI_HASH);
    CHECK(s.feed == NQ_FEED_OHLCV && s.requirement == NQ_REQUIRES_NONE && s.ncol == 4);
    CHECK(plugin.Fns().on_ohlcv != nullptr && plugin.Fns().on_event == nullptr);
    CHECK(std::strcmp(plugin.Columns()[0].name, "close") == 0);
    CHECK(plugin.Columns()[2].kind == NQ_COLUMN_CUMULATIVE);

    nq::ParamsBuilder pb;
    pb.PriceScale(100);
    nq::host::PluginInstance inst = plugin.Create(*pb.Build());

    std::vector<double> out(4);
    inst.OnOhlcv(MakeOhlcv(60, 10000, 10250, 9900, 10100, 1.5));
    inst.OnOhlcv(MakeOhlcv(120, 10100, 10300, 10000, 10200, 2.25));
    inst.Read(out, nullptr);
    CHECK(Near(out[0], 102.0) && Near(out[1], 3.0) && Near(out[2], 3.75) && out[3] == 2);

    inst.OnControl(nq_control{NQ_CONTROL_RESET, 130});
    inst.Read(out, nullptr);
    CHECK(out[2] == 0 && out[3] == 0);
    return 0;
}

int main() {
    if (int r = TestSandbox()) return r;
    if (int r = TestWarmupSandbox()) return r;
    if (int r = TestPlugin()) return r;
    if (int r = TestOhlcvSandbox()) return r;
    if (int r = TestOhlcvPlugin()) return r;
    std::printf("ok abi_hash=%llx\n", static_cast<unsigned long long>(NQ_ABI_HASH));
    return 0;
}
