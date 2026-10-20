#include <cmath>
#include <stdexcept>

#include <nq/book_view.hpp>

#include <Feature/OrderbookView.h>
#include <Pipeline.h>

#include "support.hpp"

using namespace nqtest;

static void Step(nq::Pipeline& pipe, const nq_mbo& m) {
    pipe.Engine().Advance(m.ts);
    pipe.Apply(m);
}

static int TestAdapter() {
    nq::Orderbook ob;
    ob.Apply(Add(Sec(0), 0, 1, NQ_BID, P(100.5), Q(1)));
    ob.Apply(Add(Sec(0), 0, 2, NQ_BID, P(100.4), Q(2)));
    ob.Apply(Add(Sec(0), 0, 3, NQ_ASK, P(100.7), Q(3)));

    nq::OrderbookView adapter(ob);
    nq::BookView view(&adapter.View());
    CHECK(view.Best(nq::Side::Bid)->price == P(100.5));
    CHECK(view.GetLevel(nq::Side::Bid, 1)->price == P(100.4) && view.GetLevel(nq::Side::Bid, 1)->qty == Q(2));
    CHECK(!view.GetLevel(nq::Side::Bid, 2));
    CHECK(view.GetLevel(nq::Side::Ask, 0)->count == 1);
    CHECK(!view.GetLevel(nq::Side::Ask, 1));
    CHECK(view.Ts() == 0);
    return 0;
}

static int TestPipeline() {
    nq::FeatureCatalog catalog;
    const std::size_t idx = catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "test");
    CHECK(idx == 0 && catalog.PluginCount() == 1);
    CHECK(catalog.Columns().size() == 5);
    CHECK(catalog.Columns()[0].name == "test.spread" && catalog.Columns()[0].kind == NQ_COLUMN_SNAPSHOT);
    CHECK(catalog.Columns()[2].name == "test.buy_vol" && catalog.Columns()[2].kind == NQ_COLUMN_CUMULATIVE);

    bool threw = false;
    try { catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "test"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec));
    catalog.AddSampler(nq::SamplerSpec::Time("1s_fill", kSec).FillGaps());
    catalog.AddSampler(nq::SamplerSpec::Events("3ev", 3));
    catalog.AddSampler(nq::SamplerSpec::Trades("1tr", 1).Columns({"test.trades", "test.spread"}));
    CHECK(catalog.FindSampler("1tr")->columns.size() == 2 && catalog.FindSampler("1tr")->names[0] == "test.trades");

    threw = false;
    try { catalog.AddSampler(nq::SamplerSpec::Time("bad", kSec).Columns({"nope"})); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { catalog.AddSampler(nq::SamplerSpec::Events("zero", 0)); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec)); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "late"); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);

    nq::Pipeline pipe(catalog, nq::Instrument{0, "TEST", 1, 1});
    nq::FeatureEngine& eng = pipe.Engine();
    CHECK(eng.SamplerCount() == 4 && eng.Columns().size() == 5);
    nq::Sampler& bars = eng.GetSampler(0);
    nq::Sampler& filled = eng.GetSampler(1);
    nq::Sampler& every3 = eng.GetSampler(2);
    nq::Sampler& trades = eng.GetSampler(3);
    CHECK(trades.Ncol() == 2 && trades.Rows().Columns()[0] == "test.trades");

    Step(pipe, Add(Sec(0.1), 0, 1, NQ_BID, P(100.50), Q(7)));
    Step(pipe, Add(Sec(0.2), 0, 2, NQ_ASK, P(100.70), Q(3)));
    Step(pipe, Trade(Sec(0.5), 0, 2, NQ_ASK, P(100.70), Q(2)));
    CHECK(bars.Rows().Empty());
    CHECK(every3.Rows().RowCount() == 1 && RowIs(every3.Rows(), 0, Sec(0.5), 0.20, 100.60, 2, 0, 1));

    Step(pipe, Add(Sec(1.2), 0, 3, NQ_BID, P(100.40), Q(1)));
    CHECK(bars.Rows().RowCount() == 1 && RowIs(bars.Rows(), 0, Sec(1.0), 0.20, 100.60, 2, 0, 1));
    CHECK(filled.Rows().RowCount() == 1);

    Step(pipe, Trade(Sec(1.5), 0, 1, NQ_BID, P(100.50), Q(3)));
    Step(pipe, Cancel(Sec(3.7), 0, 3));
    CHECK(bars.Rows().RowCount() == 2 && RowIs(bars.Rows(), 1, Sec(2.0), 0.20, 100.60, 0, 3, 1));
    CHECK(filled.Rows().RowCount() == 3);
    CHECK(RowIs(filled.Rows(), 1, Sec(2.0), 0.20, 100.60, 0, 3, 1));
    CHECK(RowIs(filled.Rows(), 2, Sec(3.0), 0.20, 100.60, 0, 0, 0));
    CHECK(every3.Rows().RowCount() == 2 && RowIs(every3.Rows(), 1, Sec(3.7), 0.20, 100.60, 0, 3, 1));

    Step(pipe, Clear(Sec(4.1), 0));
    CHECK(bars.Rows().RowCount() == 3 && RowIs(bars.Rows(), 2, Sec(4.0), 0.20, 100.60, 0, 0, 0));
    CHECK(filled.Rows().RowCount() == 4 && RowIs(filled.Rows(), 3, Sec(4.0), 0.20, 100.60, 0, 0, 0));
    CHECK(pipe.Book().OrderCount() == 0);

    Step(pipe, Add(Sec(4.3), 0, 4, NQ_BID, P(100.00), Q(5)));
    Step(pipe, Add(Sec(4.4), 0, 5, NQ_ASK, P(100.10), Q(5)));
    CHECK(every3.Rows().RowCount() == 3 && RowIs(every3.Rows(), 2, Sec(4.4), 0.10, 100.05, 0, 0, 0));

    Step(pipe, Trade(Sec(4.6), 0, 5, NQ_ASK, P(100.10), Q(1)));
    Step(pipe, Add(Sec(5.2), 0, 6, NQ_BID, P(99.90), Q(1)));
    CHECK(bars.Rows().RowCount() == 4 && RowIs(bars.Rows(), 3, Sec(5.0), 0.10, 100.05, 1, 0, 1));
    CHECK(filled.Rows().RowCount() == 5 && RowIs(filled.Rows(), 4, Sec(5.0), 0.10, 100.05, 1, 0, 1));
    CHECK(every3.Rows().RowCount() == 3);

    CHECK(trades.Rows().RowCount() == 3);
    CHECK(trades.Rows().Ts(0) == static_cast<std::int64_t>(0.5 * kSec) && Near(trades.Rows().At(0, 0), 1) && Near(trades.Rows().At(0, 1), 0.20));
    CHECK(trades.Rows().Ts(1) == static_cast<std::int64_t>(1.5 * kSec) && Near(trades.Rows().At(1, 0), 1));
    CHECK(trades.Rows().Ts(2) == static_cast<std::int64_t>(4.6 * kSec) && Near(trades.Rows().At(2, 0), 1) && Near(trades.Rows().At(2, 1), 0.10));

    CHECK(eng.FindSampler("1s") == &bars && eng.FindSampler("none") == nullptr);
    CHECK(pipe.Book().GetStats().adds == 6 && pipe.Book().GetStats().trades == 3);

    pipe.Close();
    CHECK(bars.Rows().RowCount() == 5 && RowIs(bars.Rows(), 4, Sec(6.0), 0.10, 100.05, 0, 0, 0));
    CHECK(filled.Rows().RowCount() == 6 && RowIs(filled.Rows(), 5, Sec(6.0), 0.10, 100.05, 0, 0, 0));
    CHECK(every3.Rows().RowCount() == 4 && RowIs(every3.Rows(), 3, Sec(5.2), 0.10, 100.05, 1, 0, 1));
    CHECK(trades.Rows().RowCount() == 3);
    pipe.Close();
    CHECK(bars.Rows().RowCount() == 5 && filled.Rows().RowCount() == 7 && every3.Rows().RowCount() == 4);
    return 0;
}

static int TestQueueDrain() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "test");
    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec));
    nq::Pipeline pipe(catalog, nq::Instrument{0, "TEST", 1, 1});
    nq::Sampler& bars = pipe.Engine().GetSampler(0);

    nq::Orderbook::Queue q(64);
    CHECK(q.try_push(Add(Sec(0.1), 0, 1, NQ_BID, P(10), Q(1))));
    CHECK(q.try_push(Add(Sec(0.2), 0, 2, NQ_ASK, P(11), Q(1))));
    CHECK(q.try_push(Trade(Sec(1.1), 0, 2, NQ_ASK, P(11), Q(1))));
    CHECK(pipe.Consume(q) == 3);
    CHECK(bars.Rows().RowCount() == 1 && RowIs(bars.Rows(), 0, Sec(1.0), 1.0, 10.5, 0, 0, 0));
    CHECK(pipe.Book().BestAsk() == nullptr);
    pipe.Close();
    CHECK(bars.Rows().RowCount() == 2 && RowIs(bars.Rows(), 1, Sec(2.0), NAN, NAN, 1, 0, 1));
    return 0;
}

int main() {
    if (int r = TestAdapter()) return r;
    if (int r = TestPipeline()) return r;
    if (int r = TestQueueDrain()) return r;
    std::printf("ok\n");
    return 0;
}
