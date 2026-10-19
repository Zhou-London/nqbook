#include <cmath>
#include <cstdio>
#include <stdexcept>

#include <nq/book_view.hpp>

#include <Feature/BookAdapter.h>
#include <Pipeline.h>

using nlib::action;
using nlib::mbo;
using nlib::side;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static constexpr std::int64_t kSec = 1'000'000'000;

static std::int64_t P(double x) { return static_cast<std::int64_t>(std::llround(x * static_cast<double>(nlib::price_scale))); }
static std::int64_t Q(double x) { return static_cast<std::int64_t>(std::llround(x * static_cast<double>(nlib::qty_scale))); }
static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static mbo Add(double t, std::uint64_t id, side s, double p, double q) {
    return mbo{static_cast<std::int64_t>(t * kSec), id, P(p), Q(q), s, action::add};
}
static mbo Cancel(double t, std::uint64_t id) {
    return mbo{static_cast<std::int64_t>(t * kSec), id, 0, 0, side::bid, action::cancel};
}
static mbo Trade(double t, std::uint64_t id, side s, double p, double q) {
    return mbo{static_cast<std::int64_t>(t * kSec), id, P(p), Q(q), s, action::trade};
}
static mbo Clear(double t) {
    return mbo{static_cast<std::int64_t>(t * kSec), 0, 0, 0, side::bid, action::clear};
}

static bool RowIs(const nq::RowBuffer& rows, std::size_t i, double ts_sec, double spread, double mid,
                  double buy, double sell, double trades) {
    if (i >= rows.RowCount()) return false;
    if (rows.Ts(i) != static_cast<std::int64_t>(ts_sec * kSec)) return false;
    const auto r = rows.Row(i);
    const bool book_ok = std::isnan(spread) ? (std::isnan(r[0]) && std::isnan(r[1]))
                                            : (Near(r[0], spread) && Near(r[1], mid));
    return book_ok && Near(r[2], buy) && Near(r[3], sell) && Near(r[4], trades);
}

static int TestAdapter() {
    nq::Orderbook ob;
    ob.Apply(Add(0, 1, side::bid, 100.5, 1));
    ob.Apply(Add(0, 2, side::bid, 100.4, 2));
    ob.Apply(Add(0, 3, side::ask, 100.7, 3));

    nq::BookAdapter adapter(ob);
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
    nq::Pipeline pipe(nq::InstrumentSpec{"TEST", 1, 1});
    nq::FeatureEngine& eng = pipe.Engine();

    const std::size_t idx = eng.LoadPlugin(NQ_TEST_PLUGIN_PATH, "test");
    CHECK(idx == 0 && eng.PluginCount() == 1);
    CHECK(eng.Columns().size() == 5);
    CHECK(eng.Columns()[0].name == "test.spread" && eng.Columns()[0].kind == NQ_COLUMN_SNAPSHOT);
    CHECK(eng.Columns()[2].name == "test.buy_vol" && eng.Columns()[2].kind == NQ_COLUMN_CUMULATIVE);

    bool threw = false;
    try { eng.LoadPlugin(NQ_TEST_PLUGIN_PATH, "test"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    nq::Sampler& bars = eng.AddSampler(nq::SamplerSpec::Time("1s", kSec));
    nq::Sampler& filled = eng.AddSampler(nq::SamplerSpec::Time("1s_fill", kSec).FillGaps());
    nq::Sampler& every3 = eng.AddSampler(nq::SamplerSpec::Events("3ev", 3));
    nq::Sampler& trades = eng.AddSampler(nq::SamplerSpec::Trades("1tr", 1).Columns({"test.trades", "test.spread"}));
    CHECK(trades.Ncol() == 2 && trades.Rows().Columns()[0] == "test.trades");

    threw = false;
    try { eng.AddSampler(nq::SamplerSpec::Time("bad", kSec).Columns({"nope"})); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { eng.AddSampler(nq::SamplerSpec::Events("zero", 0)); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { eng.LoadPlugin(NQ_TEST_PLUGIN_PATH, "late"); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);

    pipe.Apply(Add(0.1, 1, side::bid, 100.50, 7));
    pipe.Apply(Add(0.2, 2, side::ask, 100.70, 3));
    pipe.Apply(Trade(0.5, 2, side::ask, 100.70, 2));
    CHECK(bars.Rows().Empty());
    CHECK(every3.Rows().RowCount() == 1 && RowIs(every3.Rows(), 0, 0.5, 0.20, 100.60, 2, 0, 1));

    pipe.Apply(Add(1.2, 3, side::bid, 100.40, 1));
    CHECK(bars.Rows().RowCount() == 1 && RowIs(bars.Rows(), 0, 1.0, 0.20, 100.60, 2, 0, 1));
    CHECK(filled.Rows().RowCount() == 1);

    pipe.Apply(Trade(1.5, 1, side::bid, 100.50, 3));
    pipe.Apply(Cancel(3.7, 3));
    CHECK(bars.Rows().RowCount() == 2 && RowIs(bars.Rows(), 1, 2.0, 0.20, 100.60, 0, 3, 1));
    CHECK(filled.Rows().RowCount() == 3);
    CHECK(RowIs(filled.Rows(), 1, 2.0, 0.20, 100.60, 0, 3, 1));
    CHECK(RowIs(filled.Rows(), 2, 3.0, 0.20, 100.60, 0, 0, 0));
    CHECK(every3.Rows().RowCount() == 2 && RowIs(every3.Rows(), 1, 3.7, 0.20, 100.60, 0, 3, 1));

    pipe.Apply(Clear(4.1));
    CHECK(bars.Rows().RowCount() == 3 && RowIs(bars.Rows(), 2, 4.0, 0.20, 100.60, 0, 0, 0));
    CHECK(filled.Rows().RowCount() == 4 && RowIs(filled.Rows(), 3, 4.0, 0.20, 100.60, 0, 0, 0));
    CHECK(pipe.Book().OrderCount() == 0);

    pipe.Apply(Add(4.3, 4, side::bid, 100.00, 5));
    pipe.Apply(Add(4.4, 5, side::ask, 100.10, 5));
    CHECK(every3.Rows().RowCount() == 3 && RowIs(every3.Rows(), 2, 4.4, 0.10, 100.05, 0, 0, 0));

    pipe.Apply(Trade(4.6, 5, side::ask, 100.10, 1));
    pipe.Apply(Add(5.2, 6, side::bid, 99.90, 1));
    CHECK(bars.Rows().RowCount() == 4 && RowIs(bars.Rows(), 3, 5.0, 0.10, 100.05, 1, 0, 1));
    CHECK(filled.Rows().RowCount() == 5 && RowIs(filled.Rows(), 4, 5.0, 0.10, 100.05, 1, 0, 1));
    CHECK(every3.Rows().RowCount() == 3);

    CHECK(trades.Rows().RowCount() == 3);
    CHECK(trades.Rows().Ts(0) == static_cast<std::int64_t>(0.5 * kSec) && Near(trades.Rows().At(0, 0), 1) && Near(trades.Rows().At(0, 1), 0.20));
    CHECK(trades.Rows().Ts(1) == static_cast<std::int64_t>(1.5 * kSec) && Near(trades.Rows().At(1, 0), 1));
    CHECK(trades.Rows().Ts(2) == static_cast<std::int64_t>(4.6 * kSec) && Near(trades.Rows().At(2, 0), 1) && Near(trades.Rows().At(2, 1), 0.10));

    CHECK(eng.FindSampler("1s") == &bars && eng.FindSampler("none") == nullptr);
    CHECK(pipe.Book().GetStats().adds == 6 && pipe.Book().GetStats().trades == 3);
    return 0;
}

static int TestQueueDrain() {
    nq::Pipeline pipe(nq::InstrumentSpec{"TEST", 1, 1});
    pipe.Engine().LoadPlugin(NQ_TEST_PLUGIN_PATH, "test");
    nq::Sampler& bars = pipe.Engine().AddSampler(nq::SamplerSpec::Time("1s", kSec));

    nq::Orderbook::Queue q(64);
    CHECK(q.try_push(Add(0.1, 1, side::bid, 10, 1)));
    CHECK(q.try_push(Add(0.2, 2, side::ask, 11, 1)));
    CHECK(q.try_push(Trade(1.1, 2, side::ask, 11, 1)));
    CHECK(pipe.Drain(q) == 3);
    CHECK(bars.Rows().RowCount() == 1 && RowIs(bars.Rows(), 0, 1.0, 1.0, 10.5, 0, 0, 0));
    CHECK(pipe.Book().BestAsk() == nullptr);
    return 0;
}

int main() {
    if (int r = TestAdapter()) return r;
    if (int r = TestPipeline()) return r;
    if (int r = TestQueueDrain()) return r;
    std::printf("ok\n");
    return 0;
}
