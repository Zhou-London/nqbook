#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <thread>

#include <nq/adapter_host.hpp>
#include <nq/params.hpp>

#include <Market.h>
#include <QueueSink.h>
#include <Runner.h>

using nlib::action;
using nlib::mbo;
using nlib::side;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static constexpr std::int64_t kSec = 1'000'000'000;

static std::int64_t P(double x) { return static_cast<std::int64_t>(std::llround(x * static_cast<double>(nlib::price_scale))); }
static std::int64_t Q(double x) { return static_cast<std::int64_t>(std::llround(x * static_cast<double>(nlib::qty_scale))); }
static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static mbo Add(double t, std::uint32_t inst, std::uint64_t id, side s, double p, double q) {
    return mbo{static_cast<std::int64_t>(t * kSec), id, P(p), Q(q), inst, s, action::add};
}
static mbo Trade(double t, std::uint32_t inst, std::uint64_t id, side s, double p, double q) {
    return mbo{static_cast<std::int64_t>(t * kSec), id, P(p), Q(q), inst, s, action::trade};
}
static mbo Clear(double t, std::uint32_t inst) {
    return mbo{static_cast<std::int64_t>(t * kSec), 0, 0, 0, inst, side::bid, action::clear};
}

static std::optional<nq::InstrumentSpec> Resolve(std::uint32_t id) {
    if (id == 1) return nq::InstrumentSpec{"AAA", 10, 1};
    if (id == 2) return nq::InstrumentSpec{"BBB", 5, 100};
    return std::nullopt;
}

static int TestMarket() {
    nq::PluginSet set;
    set.LoadPlugin(NQ_TEST_PLUGIN_PATH, "test");
    set.AddSampler(nq::SamplerSpec::Time("1s", kSec));
    set.AddSampler(nq::SamplerSpec::Trades("t", 1).Columns({"test.trades"}));

    nq::Market market(set, &Resolve, 16);
    CHECK(market.InstrumentCount() == 0);

    market.Apply(Add(0.1, 1, 1, side::bid, 100.50, 7));
    market.Apply(Add(0.2, 2, 1, side::bid, 50.00, 3));
    market.Apply(Add(0.3, 1, 2, side::ask, 100.70, 3));
    market.Apply(Add(0.4, 2, 2, side::ask, 50.10, 3));
    CHECK(market.InstrumentCount() == 2);
    CHECK(market.Find(1)->Engine().Instrument().symbol == "AAA" && market.Find(1)->Engine().Instrument().tick_size == 10);
    CHECK(market.Find(2)->Engine().Instrument().symbol == "BBB" && market.Find(2)->Engine().Instrument().lot_size == 100);
    CHECK(market.Find(1)->Book().OrderCount() == 2 && market.Find(2)->Book().OrderCount() == 2);
    CHECK(market.Find(1)->Book().FindOrder(1)->price == P(100.50));
    CHECK(market.Find(2)->Book().FindOrder(1)->price == P(50.00));

    market.Apply(Trade(0.5, 1, 2, side::ask, 100.70, 2));
    market.Apply(Add(1.2, 1, 3, side::bid, 100.40, 1));
    market.Apply(Add(1.3, 2, 3, side::bid, 49.90, 1));

    const nq::RowBuffer& a = market.Find(1)->Engine().GetSampler(0).Rows();
    const nq::RowBuffer& b = market.Find(2)->Engine().GetSampler(0).Rows();
    CHECK(a.RowCount() == 1 && a.Ts(0) == kSec && Near(a.At(0, 0), 0.20) && Near(a.At(0, 2), 2.0) && Near(a.At(0, 4), 1));
    CHECK(b.RowCount() == 1 && b.Ts(0) == kSec && Near(b.At(0, 0), 0.10) && Near(b.At(0, 2), 0.0) && Near(b.At(0, 4), 0));
    CHECK(market.Find(1)->Engine().GetSampler(1).Rows().RowCount() == 1);
    CHECK(market.Find(2)->Engine().GetSampler(1).Rows().RowCount() == 0);

    market.Apply(Clear(1.4, 1));
    CHECK(market.Find(1)->Book().OrderCount() == 0 && market.Find(2)->Book().OrderCount() == 3);

    market.Apply(Add(1.0, 3, 9, side::bid, 1.0, 1));
    CHECK(market.InstrumentCount() == 3);
    CHECK(market.Find(3)->Engine().Instrument().symbol == "#3");
    CHECK(market.Stats().instrument_unknown == 1 && market.Entries().at(3).stats.resolved == false);
    CHECK(market.Stats().events == 9 && market.Stats().ts_regressions == 1);
    CHECK(market.Entries().at(1).stats.events == 5 && market.Entries().at(1).stats.ts_regressions == 0);
    CHECK(market.Stats().first_ts == static_cast<std::int64_t>(0.1 * kSec) && market.Stats().last_ts == kSec);
    return 0;
}

static int TestQueueSink() {
    nq::QueueSink sink(4);
    CHECK(sink.Queue().capacity() == 4);
    const nq_sink& raw = sink.Raw();

    std::thread producer([&] {
        for (std::uint64_t i = 1; i <= 100; ++i) {
            nq_mbo m{static_cast<std::int64_t>(i), i, 10, 1, 7, NQ_ASK, NQ_ADD};
            raw.push(raw.ctx, &m);
        }
        raw.end(raw.ctx, 0);
    });

    std::uint64_t got = 0;
    std::uint64_t last = 0;
    bool ok = true;
    for (;;) {
        auto m = sink.Queue().try_pop();
        if (m) {
            ++got;
            ok = ok && m->id == last + 1 && m->instrument == 7 && m->side == side::ask && m->action == action::add;
            last = m->id;
            continue;
        }
        if (sink.Ended() && sink.Queue().empty()) break;
        std::this_thread::yield();
    }
    producer.join();
    CHECK(ok && got == 100 && sink.Pushed() == 100 && sink.Stalls() > 0 && sink.Status() == 0);

    nq::QueueSink blocked(1);
    const nq_sink& braw = blocked.Raw();
    nq_mbo m{1, 1, 1, 1, 1, NQ_BID, NQ_ADD};
    braw.push(braw.ctx, &m);
    std::atomic<bool> returned{false};
    std::thread t([&] {
        braw.push(braw.ctx, &m);
        returned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(!returned);
    blocked.Abort();
    t.join();
    CHECK(returned && blocked.Pushed() == 1);
    return 0;
}

static int TestRunner() {
    nq::PluginSet set;
    set.LoadPlugin(NQ_TEST_PLUGIN_PATH, "test");
    set.AddSampler(nq::SamplerSpec::Events("e", 10));

    nq::host::AdapterPlugin adapter(NQ_TEST_ADAPTER_PATH);
    nq::ParamsBuilder pb;
    pb.Set("count", "1000").Set("instruments", "3");
    nq::Runner runner(set, adapter, *pb.Build(), 8);
    std::atomic<bool> stop{false};
    const nq::RunResult r = runner.Run(stop);
    CHECK(!r.interrupted && r.adapter_status == 0 && r.consumed == 1000);
    const nq::Market& m = runner.GetMarket();
    CHECK(m.InstrumentCount() == 3 && m.Stats().events == 1000 && m.Stats().instrument_unknown == 0);
    CHECK(m.Find(1)->Engine().Instrument().symbol == "SYN1" && m.Find(3)->Engine().Instrument().symbol == "SYN3");
    CHECK(m.Find(1)->Book().OrderCount() == 334 && m.Find(2)->Book().OrderCount() == 333);
    CHECK(m.Find(1)->Engine().GetSampler(0).Rows().RowCount() == 33);
    CHECK(m.Entries().at(2).stats.events == 333);

    nq::ParamsBuilder endless;
    endless.Set("count", "1").Set("endless", "true").Set("delay_us", "50");
    nq::Runner forever(set, adapter, *endless.Build(), 8);
    std::atomic<bool> stop2{false};
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        stop2 = true;
    });
    const nq::RunResult r2 = forever.Run(stop2);
    stopper.join();
    CHECK(r2.interrupted && r2.consumed > 0 && r2.adapter_status == 0);
    return 0;
}

int main() {
    if (int r = TestMarket()) return r;
    if (int r = TestQueueSink()) return r;
    if (int r = TestRunner()) return r;
    std::printf("ok\n");
    return 0;
}
