#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>
#include <thread>

#include <nq/adapter_host.hpp>
#include <nq/params.hpp>

#include <Adapter/AdapterEngine.h>
#include <PipelineTable.h>
#include <Session.h>

#include "support.hpp"

using namespace nqtest;

static std::optional<nq::Instrument> Resolve(std::uint32_t id) {
    if (id == 1) return nq::Instrument{1, "AAA", 10, 1};
    if (id == 2) return nq::Instrument{2, "BBB", 5, 100};
    return std::nullopt;
}

static int TestMarket() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "test");
    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec));
    catalog.AddSampler(nq::SamplerSpec::Trades("t", 1).Columns({"test.trades"}));

    nq::PipelineTable table(catalog, &Resolve, 16, 4);
    CHECK(table.InstrumentCount() == 0 && table.Entries().size() == 4 && table.Find(9) == nullptr);

    table.Apply(Add(Sec(0.1), 1, 1, NQ_BID, P(100.50), Q(7)));
    table.Apply(Add(Sec(0.2), 2, 1, NQ_BID, P(50.00), Q(3)));
    table.Apply(Add(Sec(0.3), 1, 2, NQ_ASK, P(100.70), Q(3)));
    table.Apply(Add(Sec(0.4), 2, 2, NQ_ASK, P(50.10), Q(3)));
    CHECK(table.InstrumentCount() == 2);
    CHECK(table.Find(1)->Engine().GetInstrument().symbol == "AAA" && table.Find(1)->Engine().GetInstrument().tick_size == 10);
    CHECK(table.Find(2)->Engine().GetInstrument().symbol == "BBB" && table.Find(2)->Engine().GetInstrument().lot_size == 100);
    CHECK(table.Find(1)->Book().OrderCount() == 2 && table.Find(2)->Book().OrderCount() == 2);
    CHECK(table.Find(1)->Book().FindOrder(1)->price == P(100.50));
    CHECK(table.Find(2)->Book().FindOrder(1)->price == P(50.00));

    table.Apply(Trade(Sec(0.5), 1, 2, NQ_ASK, P(100.70), Q(2)));
    table.Apply(Add(Sec(1.2), 1, 3, NQ_BID, P(100.40), Q(1)));
    table.Apply(Add(Sec(1.3), 2, 3, NQ_BID, P(49.90), Q(1)));

    const nq::RowBuffer& a = table.Find(1)->Engine().GetSampler(0).Rows();
    const nq::RowBuffer& b = table.Find(2)->Engine().GetSampler(0).Rows();
    CHECK(a.RowCount() == 1 && a.Ts(0) == kSec && Near(a.At(0, 0), 0.20) && Near(a.At(0, 2), 2.0) && Near(a.At(0, 4), 1));
    CHECK(b.RowCount() == 1 && b.Ts(0) == kSec && Near(b.At(0, 0), 0.10) && Near(b.At(0, 2), 0.0) && Near(b.At(0, 4), 0));
    CHECK(table.Find(1)->Engine().GetSampler(1).Rows().RowCount() == 1);
    CHECK(table.Find(2)->Engine().GetSampler(1).Rows().RowCount() == 0);

    table.Apply(Clear(Sec(1.4), 1));
    CHECK(table.Find(1)->Book().OrderCount() == 0 && table.Find(2)->Book().OrderCount() == 3);

    table.Apply(Add(Sec(1.0), 3, 9, NQ_BID, P(1.0), Q(1)));
    CHECK(table.InstrumentCount() == 3);
    CHECK(table.Find(3)->Engine().GetInstrument().symbol == "#3");
    CHECK(table.Stats().instrument_unknown == 1 && table.Entries()[3].stats.resolved == false);
    CHECK(table.Stats().events == 9 && table.Stats().ts_regressions == 1);
    CHECK(table.Entries()[1].stats.events == 5 && table.Entries().at(1).stats.ts_regressions == 0);
    CHECK(table.Stats().first_ts == static_cast<std::int64_t>(0.1 * kSec) && table.Stats().last_ts == kSec);

    table.Apply(Add(Sec(2.5), 1, 7, NQ_BID, P(100.00), Q(1)));
    CHECK(a.RowCount() == 2 && a.Ts(1) == 2 * kSec && std::isnan(a.At(1, 0)));
    CHECK(b.RowCount() == 2 && b.Ts(1) == 2 * kSec && Near(b.At(1, 0), 0.10) && Near(b.At(1, 4), 0));
    const nq::RowBuffer& c = table.Find(3)->Engine().GetSampler(0).Rows();
    CHECK(c.RowCount() == 1 && c.Ts(0) == 2 * kSec);

    table.Close();
    CHECK(a.RowCount() == 3 && a.Ts(2) == 3 * kSec && std::isnan(a.At(2, 0)));
    CHECK(b.RowCount() == 2);
    CHECK(c.RowCount() == 1);

    table.Apply(Add(Sec(2.6), 700, 1, NQ_BID, P(1.0), Q(1)));
    CHECK(table.InstrumentCount() == 4 && table.Entries().size() == 701 && table.Entries()[700].id == 700);
    CHECK(table.Find(700)->Engine().GetInstrument().symbol == "#700" && table.Find(699) == nullptr);
    return 0;
}

static int TestSession() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "test");
    catalog.AddSampler(nq::SamplerSpec::Events("e", 10));

    nq::host::AdapterPlugin adapter(NQ_SYNTHETIC_ADAPTER_PATH);
    nq::ParamsBuilder pb;
    pb.Set("count", "1000").Set("instruments", "3");
    nq::Session session(catalog, adapter, *pb.Build(), 8);
    std::atomic<bool> stop{false};
    const nq::RunResult r = session.Run(stop);
    CHECK(!r.interrupted && r.adapter_status == 0 && r.consumed == 1000);
    const nq::PipelineTable& m = session.Pipelines();
    CHECK(m.InstrumentCount() == 3 && m.Stats().events == 1000 && m.Stats().instrument_unknown == 0);
    CHECK(m.Find(1)->Engine().GetInstrument().symbol == "SYN1" && m.Find(3)->Engine().GetInstrument().symbol == "SYN3");
    CHECK(m.Find(1)->Book().OrderCount() == 334 && m.Find(2)->Book().OrderCount() == 333);
    CHECK(m.Find(1)->Engine().GetSampler(0).Rows().RowCount() == 34);
    CHECK(m.Find(2)->Engine().GetSampler(0).Rows().RowCount() == 34 && m.Find(3)->Engine().GetSampler(0).Rows().RowCount() == 34);
    CHECK(m.Entries()[2].stats.events == 333);

    nq::ParamsBuilder endless;
    endless.Set("count", "1").Set("endless", "true");
    nq::Session forever(catalog, adapter, *endless.Build(), 8);
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
    if (int r = TestSession()) return r;
    std::printf("ok\n");
    return 0;
}
