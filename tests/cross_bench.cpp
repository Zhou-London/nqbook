#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

#include <PipelineTable.h>

#include "support.hpp"

using namespace nqtest;

namespace {

constexpr std::int64_t kMs = 1'000'000;

struct CountingSink : nq::RowSink {
    void Register(const nq::Instrument&) override {}
    void Push(nq::FeatureBlock block) override {
        rows += block.instruments.size();
        ++blocks;
        spare = std::move(block);
    }
    nq::FeatureBlock Acquire(std::uint32_t) override { return std::move(spare); }

    std::uint64_t rows = 0;
    std::uint64_t blocks = 0;
    nq::FeatureBlock spare;
};

struct ZScore : nq::CrossOperator {
    void Apply(const nq::CrossView& v) override {
        const auto in = v.In(0);
        const auto out = v.Out(0);
        double sum = 0.0;
        double sq = 0.0;
        double n = 0.0;
        for (const double x : in) {
            const bool ok = x == x;
            sum += ok ? x : 0.0;
            sq += ok ? x * x : 0.0;
            n += ok ? 1.0 : 0.0;
        }
        const double mean = sum / n;
        const double sd = std::sqrt(std::max(sq / n - mean * mean, 0.0));
        for (std::size_t r = 0; r < in.size(); ++r) out[r] = (in[r] - mean) / sd;
    }
};

struct Rank : nq::CrossOperator {
    void Apply(const nq::CrossView& v) override {
        const auto in = v.In(0);
        const auto out = v.Out(0);
        order_.clear();
        for (std::uint32_t r = 0; r < v.RowCount(); ++r) {
            if (in[r] == in[r]) order_.push_back(r);
            else out[r] = in[r];
        }
        std::sort(order_.begin(), order_.end(), [&](std::uint32_t a, std::uint32_t b) { return in[a] < in[b]; });
        const double last = order_.size() > 1 ? static_cast<double>(order_.size() - 1) : 1.0;
        for (std::size_t i = 0; i < order_.size(); ++i) out[order_[i]] = static_cast<double>(i) / last;
    }

private:
    std::vector<std::uint32_t> order_;
};

struct Noop : nq::CrossOperator {
    void Apply(const nq::CrossView&) override {}
};

template <class Op>
nq::CrossSpec Cross(std::int64_t every, bool fill_gaps) {
    nq::CrossSpec spec;
    spec.name = "x";
    spec.bars = {every, fill_gaps};
    spec.inputs = {"f.mid"};
    spec.outputs = {"out"};
    spec.make = [] { return std::make_unique<Op>(); };
    return spec;
}

struct Feed {
    std::vector<nq_mbo> seed;
    std::vector<nq_mbo> events;
    std::uint64_t bars = 0;
};

Feed MakeFeed(std::uint32_t instruments, std::size_t count, std::int64_t per_ms) {
    Feed feed;
    for (std::uint32_t i = 0; i < instruments; ++i) {
        feed.seed.push_back(Add(1, i, 1, NQ_BID, P(100.0), Q(1)));
        feed.seed.push_back(Add(1, i, 2, NQ_ASK, P(101.0), Q(1)));
    }
    std::mt19937_64 rng(7);
    const std::int64_t step = kMs / per_ms;
    for (std::size_t i = 0; i < count; ++i) {
        const auto instrument = static_cast<std::uint32_t>(rng() % instruments);
        const double price = 99.0 + static_cast<double>(rng() % 100) / 100.0;
        feed.events.push_back(Modify(kMs + static_cast<std::int64_t>(i) * step, instrument, 1, P(price), Q(1)));
    }
    feed.bars = count / static_cast<std::size_t>(per_ms);
    return feed;
}

struct Result {
    double ns_per_event = 0.0;
    std::uint64_t rows = 0;
    std::uint64_t blocks = 0;
};

void LoadExample(nq::FeatureCatalog& c) { c.LoadPlugin(NQ_EXAMPLE_FEATURES_PATH, "f"); }
void LoadCrossPlugin(nq::FeatureCatalog& c) { c.LoadPlugin(NQ_EXAMPLE_CROSS_FEATURES_PATH, "c"); }

template <class Configure>
Result Run(const Feed& feed, Configure&& configure) {
    Result best;
    for (int rep = 0; rep < 3; ++rep) {
        nq::FeatureCatalog catalog;
        configure(catalog);
        CountingSink sink;
        nq::PipelineTable<nq_mbo> table(catalog, nullptr, {.expected_orders = 8}, 8192, &sink);
        for (const nq_mbo& m : feed.seed) table.Apply(m);

        const auto t0 = std::chrono::steady_clock::now();
        for (const nq_mbo& m : feed.events) table.Apply(m);
        const auto t1 = std::chrono::steady_clock::now();

        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(feed.events.size());
        if (rep == 0 || ns < best.ns_per_event) best = Result{ns, sink.rows, sink.blocks};
    }
    return best;
}

void Print(const char* name, const Result& r, const Result& base, const Feed& feed) {
    const double extra_us = (r.ns_per_event - base.ns_per_event) * static_cast<double>(feed.events.size()) /
                            static_cast<double>(feed.bars) / 1000.0;
    std::printf("%-28s %8.1f ns/event  %+9.2f us/bar  rows=%llu blocks=%llu\n", name, r.ns_per_event, extra_us,
                static_cast<unsigned long long>(r.rows), static_cast<unsigned long long>(r.blocks));
}

}  // namespace

int main(int argc, char** argv) {
    const auto instruments = static_cast<std::uint32_t>(argc > 1 ? std::atoi(argv[1]) : 5000);
    const std::size_t count = argc > 2 ? static_cast<std::size_t>(std::atoll(argv[2])) : 2'000'000;
    const std::int64_t per_ms = argc > 3 ? std::atoll(argv[3]) : 200;

    const Feed feed = MakeFeed(instruments, count, per_ms);
    std::printf("instruments=%u events=%zu events_per_ms=%lld bars=%llu (1ms bars)\n", instruments, count,
                static_cast<long long>(per_ms), static_cast<unsigned long long>(feed.bars));

    const Result base = Run(feed, LoadExample);
    Print("no sampler", base, base, feed);
    Print("row sampler 1ms", Run(feed, [](nq::FeatureCatalog& c) {
              LoadExample(c);
              c.AddSampler(nq::SamplerSpec::Time("s", kMs).Columns({"f.mid"}));
          }), base, feed);
    Print("row sampler 1ms fill", Run(feed, [](nq::FeatureCatalog& c) {
              LoadExample(c);
              c.AddSampler(nq::SamplerSpec::Time("s", kMs, true).Columns({"f.mid"}));
          }), base, feed);
    Print("cross noop", Run(feed, [](nq::FeatureCatalog& c) {
              LoadExample(c);
              c.AddCross(Cross<Noop>(kMs, false));
          }), base, feed);
    Print("cross zscore", Run(feed, [](nq::FeatureCatalog& c) {
              LoadExample(c);
              c.AddCross(Cross<ZScore>(kMs, false));
          }), base, feed);
    Print("cross zscore fill", Run(feed, [](nq::FeatureCatalog& c) {
              LoadExample(c);
              c.AddCross(Cross<ZScore>(kMs, true));
          }), base, feed);
    Print("cross rank", Run(feed, [](nq::FeatureCatalog& c) {
              LoadExample(c);
              c.AddCross(Cross<Rank>(kMs, false));
          }), base, feed);

    const Result plugin_base = Run(feed, LoadCrossPlugin);
    Print("plugin, no cross", plugin_base, plugin_base, feed);
    Print("plugin cross mean+z+rank", Run(feed, [](nq::FeatureCatalog& c) {
              LoadCrossPlugin(c);
              c.AddCross("x", "c", kMs);
          }), plugin_base, feed);
    Print("plugin cross, fill", Run(feed, [](nq::FeatureCatalog& c) {
              LoadCrossPlugin(c);
              c.AddCross("x", "c", kMs, true);
          }), plugin_base, feed);
    return 0;
}
