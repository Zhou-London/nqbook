#include <stdexcept>

#include <nq/book_view.hpp>

#include <Feature/OrderbookView.h>
#include <Pipeline.h>

#include "support.hpp"

using namespace nqtest;

static void Step(nq::Pipeline<nq_mbo>& pipe, const nq_mbo& m) {
    pipe.Engine().Advance(m.ts);
    pipe.Apply(m);
}

static int TestAdapter() {
    nq::Orderbook ob;
    ob.Apply(Add(Sec(0), 0, 1, NQ_BID, P(100.5), Q(1)));
    ob.Apply(Add(Sec(0), 0, 2, NQ_BID, P(100.4), Q(2)));
    ob.Apply(Add(Sec(0), 0, 3, NQ_ASK, P(100.7), Q(3)));

    const nq_book_view adapter = nq::MakeBookView(ob);
    nq::BookView view(&adapter);
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
    CHECK(idx == 0 && catalog.Plugins().size() == 1);
    CHECK(catalog.Columns().size() == 5);
    CHECK(catalog.Columns()[0].name == "test.spread" && catalog.Columns()[0].kind == NQ_COLUMN_SNAPSHOT);
    CHECK(catalog.Columns()[2].name == "test.buy_vol" && catalog.Columns()[2].kind == NQ_COLUMN_CUMULATIVE);

    CHECK(Throws<std::invalid_argument>([&] { catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "test"); }));

    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec));
    catalog.AddSampler(nq::SamplerSpec::Time("1s_fill", kSec, true));
    catalog.AddSampler(nq::SamplerSpec::Events("3ev", 3));
    catalog.AddSampler(nq::SamplerSpec::Trades("1tr", 1).Columns({"test.trades", "test.spread"}));
    CHECK(catalog.FindSampler("1tr")->columns.size() == 2 && catalog.FindSampler("1tr")->names[0] == "test.trades");

    CHECK(Throws<std::invalid_argument>([&] { catalog.AddSampler(nq::SamplerSpec::Time("bad", kSec).Columns({"nope"})); }));
    CHECK(Throws<std::invalid_argument>([&] { catalog.AddSampler(nq::SamplerSpec::Events("zero", 0)); }));
    CHECK(Throws<std::invalid_argument>([&] { catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec)); }));
    CHECK(Throws<std::logic_error>([&] { catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "late"); }));

    nq::Pipeline<nq_mbo> pipe(catalog, nq::Instrument{0, "TEST", 1, 1});
    nq::FeatureEngine& eng = pipe.Engine();
    CHECK(eng.SamplerCount() == 4);
    nq::Sampler& bars = eng.GetSampler(0);
    nq::Sampler& filled = eng.GetSampler(1);
    nq::Sampler& every3 = eng.GetSampler(2);
    nq::Sampler& trades = eng.GetSampler(3);
    CHECK(trades.Ncol() == 2 && trades.Rows().Columns()[0] == "test.trades");

    Step(pipe, Add(Sec(0.1), 0, 1, NQ_BID, P(100.50), Q(7)));
    Step(pipe, Add(Sec(0.2), 0, 2, NQ_ASK, P(100.70), Q(3)));
    Step(pipe, Trade(Sec(0.5), 0, 2, NQ_ASK, P(100.70), Q(2)));
    CHECK(bars.Rows().Empty());
    CHECK(every3.Rows().RowCount() == 1 && RowIs(every3.Rows(), 0, Sec(0.5), {.spread = 0.20, .mid = 100.60, .buy_vol = 2, .trades = 1}));

    Step(pipe, Add(Sec(1.2), 0, 3, NQ_BID, P(100.40), Q(1)));
    CHECK(bars.Rows().RowCount() == 1 && RowIs(bars.Rows(), 0, Sec(1.0), {.spread = 0.20, .mid = 100.60, .buy_vol = 2, .trades = 1}));
    CHECK(filled.Rows().RowCount() == 1);

    Step(pipe, Trade(Sec(1.5), 0, 1, NQ_BID, P(100.50), Q(3)));
    Step(pipe, Cancel(Sec(3.7), 0, 3));
    CHECK(bars.Rows().RowCount() == 2 && RowIs(bars.Rows(), 1, Sec(2.0), {.spread = 0.20, .mid = 100.60, .sell_vol = 3, .trades = 1}));
    CHECK(filled.Rows().RowCount() == 3);
    CHECK(RowIs(filled.Rows(), 1, Sec(2.0), {.spread = 0.20, .mid = 100.60, .sell_vol = 3, .trades = 1}));
    CHECK(RowIs(filled.Rows(), 2, Sec(3.0), {.spread = 0.20, .mid = 100.60}));
    CHECK(every3.Rows().RowCount() == 2 && RowIs(every3.Rows(), 1, Sec(3.7), {.spread = 0.20, .mid = 100.60, .sell_vol = 3, .trades = 1}));

    Step(pipe, Clear(Sec(4.1), 0));
    CHECK(bars.Rows().RowCount() == 3 && RowIs(bars.Rows(), 2, Sec(4.0), {.spread = 0.20, .mid = 100.60}));
    CHECK(filled.Rows().RowCount() == 4 && RowIs(filled.Rows(), 3, Sec(4.0), {.spread = 0.20, .mid = 100.60}));
    CHECK(pipe.Book().OrderCount() == 0);

    Step(pipe, Add(Sec(4.3), 0, 4, NQ_BID, P(100.00), Q(5)));
    Step(pipe, Add(Sec(4.4), 0, 5, NQ_ASK, P(100.10), Q(5)));
    CHECK(every3.Rows().RowCount() == 3 && RowIs(every3.Rows(), 2, Sec(4.4), {.spread = 0.10, .mid = 100.05}));

    Step(pipe, Trade(Sec(4.6), 0, 5, NQ_ASK, P(100.10), Q(1)));
    Step(pipe, Add(Sec(5.2), 0, 6, NQ_BID, P(99.90), Q(1)));
    CHECK(bars.Rows().RowCount() == 4 && RowIs(bars.Rows(), 3, Sec(5.0), {.spread = 0.10, .mid = 100.05, .buy_vol = 1, .trades = 1}));
    CHECK(filled.Rows().RowCount() == 5 && RowIs(filled.Rows(), 4, Sec(5.0), {.spread = 0.10, .mid = 100.05, .buy_vol = 1, .trades = 1}));
    CHECK(every3.Rows().RowCount() == 3);

    CHECK(trades.Rows().RowCount() == 3);
    CHECK(trades.Rows().Ts(0) == Sec(0.5) && Near(trades.Rows().At(0, 0), 1) && Near(trades.Rows().At(0, 1), 0.20));
    CHECK(trades.Rows().Ts(1) == Sec(1.5) && Near(trades.Rows().At(1, 0), 1));
    CHECK(trades.Rows().Ts(2) == Sec(4.6) && Near(trades.Rows().At(2, 0), 1) && Near(trades.Rows().At(2, 1), 0.10));

    CHECK(pipe.Book().GetStats().adds == 6 && pipe.Book().GetStats().trades == 3);

    pipe.Close();
    CHECK(bars.Rows().RowCount() == 5 && RowIs(bars.Rows(), 4, Sec(6.0), {.spread = 0.10, .mid = 100.05}));
    CHECK(filled.Rows().RowCount() == 6 && RowIs(filled.Rows(), 5, Sec(6.0), {.spread = 0.10, .mid = 100.05}));
    CHECK(every3.Rows().RowCount() == 4 && RowIs(every3.Rows(), 3, Sec(5.2), {.spread = 0.10, .mid = 100.05, .buy_vol = 1, .trades = 1}));
    CHECK(trades.Rows().RowCount() == 3);
    pipe.Close();
    CHECK(bars.Rows().RowCount() == 5 && filled.Rows().RowCount() == 7 && every3.Rows().RowCount() == 4);
    return 0;
}

static int TestClearMidBar() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "test");
    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec).Columns({"test.buy_vol", "test.trades"}));
    nq::Pipeline<nq_mbo> pipe(catalog, nq::Instrument{0, "TEST", 1, 1});
    const nq::RowBuffer& rows = pipe.Engine().GetSampler(0).Rows();

    Step(pipe, Add(Sec(0.1), 0, 1, NQ_ASK, P(100.70), Q(5)));
    Step(pipe, Trade(Sec(0.5), 0, 1, NQ_ASK, P(100.70), Q(2)));
    Step(pipe, Clear(Sec(0.7), 0));
    Step(pipe, Add(Sec(0.8), 0, 2, NQ_ASK, P(100.70), Q(5)));
    Step(pipe, Trade(Sec(0.9), 0, 2, NQ_ASK, P(100.70), Q(1)));
    Step(pipe, Add(Sec(1.2), 0, 3, NQ_ASK, P(100.80), Q(5)));
    CHECK(rows.RowCount() == 1 && rows.Ts(0) == Sec(1.0));
    CHECK(Near(rows.At(0, 0), 3) && Near(rows.At(0, 1), 2));

    Step(pipe, Add(Sec(2.2), 0, 4, NQ_ASK, P(100.90), Q(5)));
    CHECK(rows.RowCount() == 2 && Near(rows.At(1, 0), 0) && Near(rows.At(1, 1), 0));
    return 0;
}

static int TestClearIgnoredByPlugin() {
    nq::FeatureCatalog catalog;
    catalog.LoadPlugin(NQ_IGNORE_RESET_FEATURES_PATH, "test");
    catalog.AddSampler(nq::SamplerSpec::Time("1s", kSec));
    nq::Pipeline<nq_mbo> pipe(catalog, nq::Instrument{0, "TEST", 1, 1});
    const nq::RowBuffer& rows = pipe.Engine().GetSampler(0).Rows();
    const double lot = static_cast<double>(Q(1));

    Step(pipe, Add(Sec(0.1), 0, 1, NQ_ASK, P(100.70), Q(50)));
    Step(pipe, Trade(Sec(0.5), 0, 1, NQ_ASK, P(100.70), Q(10)));
    Step(pipe, Trade(Sec(1.5), 0, 1, NQ_ASK, P(100.70), Q(4)));
    Step(pipe, Clear(Sec(2.5), 0));
    Step(pipe, Add(Sec(3.2), 0, 2, NQ_ASK, P(100.70), Q(5)));
    CHECK(rows.RowCount() == 3);
    CHECK(Near(rows.At(0, 0), 10 * lot) && Near(rows.At(1, 0), 4 * lot) && Near(rows.At(2, 0), 0));
    return 0;
}

int main() {
    if (int r = TestAdapter()) return r;
    if (int r = TestPipeline()) return r;
    if (int r = TestClearMidBar()) return r;
    if (int r = TestClearIgnoredByPlugin()) return r;
    std::printf("ok\n");
    return 0;
}
