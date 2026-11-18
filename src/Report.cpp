#include <Report.h>

#include <cinttypes>
#include <string_view>

namespace nq {

namespace {

const char* RequiresName(std::uint8_t r) {
    switch (r) {
        case NQ_REQUIRES_NONE: return "none";
        case NQ_REQUIRES_L1: return "L1";
        case NQ_REQUIRES_MBP: return "MBP";
        case NQ_REQUIRES_MBO: return "MBO";
    }
    return "?";
}

const char* KindName(std::uint8_t k) { return k == NQ_COLUMN_CUMULATIVE ? "cumulative" : "snapshot"; }

const char* TriggerName(const TimeBars&) { return "time"; }
const char* TriggerName(const EveryEvents&) { return "events"; }
const char* TriggerName(const EveryTrades&) { return "trades"; }
const char* TriggerName(const SamplerTrigger& trigger) {
    return std::visit([](const auto& t) { return TriggerName(t); }, trigger);
}

bool FillsGaps(const SamplerTrigger& trigger) {
    const auto* bars = std::get_if<TimeBars>(&trigger);
    return bars && bars->fill_gaps;
}

std::string JsonString(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    out += '"';
    return out;
}

void PrintJsonStrings(std::FILE* out, const std::vector<std::string>& strings) {
    const char* separator = "";
    for (const std::string& s : strings) {
        std::fprintf(out, "%s%s", separator, JsonString(s).c_str());
        separator = ", ";
    }
}

struct BookCounter {
    const char* name;
    std::uint64_t Orderbook::Stats::* field;
};

constexpr BookCounter kBookEvents[] = {
    {"adds", &Orderbook::Stats::adds},         {"cancels", &Orderbook::Stats::cancels},
    {"modifies", &Orderbook::Stats::modifies}, {"trades", &Orderbook::Stats::trades},
    {"clears", &Orderbook::Stats::clears},
};

constexpr BookCounter kBookAnomalies[] = {
    {"add_duplicate", &Orderbook::Stats::add_duplicate},
    {"cancel_unknown", &Orderbook::Stats::cancel_unknown},
    {"modify_unknown", &Orderbook::Stats::modify_unknown},
    {"trade_unknown", &Orderbook::Stats::trade_unknown},
    {"trade_over_order", &Orderbook::Stats::trade_over_order},
    {"trade_over_level", &Orderbook::Stats::trade_over_level},
    {"qty_invalid", &Orderbook::Stats::qty_invalid},
};

void Accumulate(Orderbook::Stats& total, const Orderbook::Stats& s) {
    for (const auto& c : kBookEvents) total.*c.field += s.*c.field;
    for (const auto& c : kBookAnomalies) total.*c.field += s.*c.field;
}

void PrintBookStats(std::FILE* out, const Orderbook::Stats& s) {
    const char* separator = "";
    for (const auto& c : kBookEvents) {
        std::fprintf(out, "%s%s=%" PRIu64, separator, c.name, s.*c.field);
        separator = " ";
    }
    std::fprintf(out, " |");
    for (const auto& c : kBookAnomalies) std::fprintf(out, " %s=%" PRIu64, c.name, s.*c.field);
}

template <class Record>
void PrintFeedSummary(std::FILE* out, const PipelineTable<Record>& table, const RunResult& result) {
    const FeedStats& f = table.Stats();
    std::fprintf(out, "feed  events=%" PRIu64 " instruments=%zu ts_regressions=%" PRIu64 " instrument_unknown=%" PRIu64 " stalls=%" PRIu64 " warmup_bars=%" PRIu64 " first_ts=%" PRId64 " last_ts=%" PRId64 " adapter_status=%d%s\n",
                 f.events, table.InstrumentCount(), f.ts_regressions, f.instrument_unknown, result.stalls, f.warmup_bars, f.first_ts,
                 f.last_ts, result.adapter_status, result.interrupted ? " interrupted" : "");

    if (result.writer) {
        const WriterResult& w = *result.writer;
        std::fprintf(out, "writer  rows=%" PRIu64 " stalls=%" PRIu64 "%s%s\n", w.rows, w.stalls, w.error.empty() ? "" : " error=", w.error.c_str());
    }
}

template <class Entry>
void PrintInstrumentSummary(std::FILE* out, const Entry& e) {
    const Instrument& spec = e.pipeline->Engine().GetInstrument();
    std::fprintf(out, "  %-6u %-16s %s events=%" PRIu64 " ts_regressions=%" PRIu64, spec.id, spec.symbol.c_str(),
                 e.stats.resolved ? "" : "(unresolved)", e.stats.events, e.stats.ts_regressions);
}

}  // namespace

void PrintCatalogText(std::FILE* out, const FeatureCatalog& catalog) {
    std::fprintf(out, "plugins (%zu)\n", catalog.Plugins().size());
    for (const LoadedPlugin& loaded : catalog.Plugins()) {
        const host::Plugin& p = loaded.library;
        std::fprintf(out, "  %-16s %s  sdk=%s feed=%s requires=%s cols=%u", loaded.name.c_str(),
                     p.Path().c_str(), p.Schema().sdk_version, FeedName(FeedOf(p)),
                     RequiresName(p.Schema().requirement), p.Schema().ncol);
        for (const auto& [k, v] : loaded.params) std::fprintf(out, "  %s=%s", k.c_str(), v.c_str());
        std::fprintf(out, "\n");
    }

    std::fprintf(out, "columns (%zu)\n", catalog.Columns().size());
    for (const auto& c : catalog.Columns()) {
        std::fprintf(out, "  %-32s %-10s %-10s %s\n", c.name.c_str(), c.unit.c_str(), KindName(c.kind), c.doc.c_str());
    }

    std::fprintf(out, "samplers (%zu)\n", catalog.Samplers().size());
    for (const auto& t : catalog.Samplers()) {
        const auto& s = t.spec;
        std::fprintf(out, "  %-16s %s every=%" PRId64 "%s fill_gaps=%s cols=%zu\n", s.name.c_str(), TriggerName(s.trigger),
                     s.Every(), std::holds_alternative<TimeBars>(s.trigger) ? "ns" : "",
                     FillsGaps(s.trigger) ? "yes" : "no", t.columns.size());
    }

    if (catalog.Crosses().empty()) return;
    std::fprintf(out, "crosses (%zu)\n", catalog.Crosses().size());
    for (const auto& t : catalog.Crosses()) {
        const auto& s = t.spec;
        std::fprintf(out, "  %-16s %s every=%" PRId64 "ns fill_gaps=%s inputs=%zu cols=%zu\n", s.name.c_str(),
                     t.plugin ? catalog.Plugins()[*t.plugin].name.c_str() : "-", s.bars.every,
                     s.bars.fill_gaps ? "yes" : "no", t.inputs.size(), t.names.size());
    }
}

void PrintCatalogJson(std::FILE* out, const FeatureCatalog& catalog) {
    std::fprintf(out, "{\n  \"plugins\": [");
    for (std::size_t i = 0; i < catalog.Plugins().size(); ++i) {
        const LoadedPlugin& loaded = catalog.Plugins()[i];
        const host::Plugin& p = loaded.library;
        std::fprintf(out, "%s\n    {\"name\": %s, \"path\": %s, \"sdk_version\": %s, \"feed\": \"%s\", \"requires\": \"%s\", \"ncol\": %u, \"params\": {",
                     i ? "," : "", JsonString(loaded.name).c_str(), JsonString(p.Path()).c_str(),
                     JsonString(p.Schema().sdk_version).c_str(), FeedName(FeedOf(p)),
                     RequiresName(p.Schema().requirement), p.Schema().ncol);
        const auto& params = loaded.params;
        for (std::size_t j = 0; j < params.size(); ++j) {
            std::fprintf(out, "%s%s: %s", j ? ", " : "", JsonString(params[j].first).c_str(),
                         JsonString(params[j].second).c_str());
        }
        std::fprintf(out, "}}");
    }
    std::fprintf(out, "\n  ],\n  \"columns\": [");
    for (std::size_t i = 0; i < catalog.Columns().size(); ++i) {
        const auto& c = catalog.Columns()[i];
        std::fprintf(out, "%s\n    {\"name\": %s, \"unit\": %s, \"kind\": \"%s\", \"doc\": %s, \"plugin\": %s}",
                     i ? "," : "", JsonString(c.name).c_str(), JsonString(c.unit).c_str(), KindName(c.kind),
                     JsonString(c.doc).c_str(), JsonString(catalog.Plugins()[c.plugin_index].name).c_str());
    }
    std::fprintf(out, "\n  ],\n  \"samplers\": [");
    for (std::size_t i = 0; i < catalog.Samplers().size(); ++i) {
        const auto& t = catalog.Samplers()[i];
        const auto& s = t.spec;
        std::fprintf(out, "%s\n    {\"name\": %s, \"trigger\": \"%s\", \"every\": %" PRId64 ", \"fill_gaps\": %s, \"columns\": [",
                     i ? "," : "", JsonString(s.name).c_str(), TriggerName(s.trigger),
                     s.Every(), FillsGaps(s.trigger) ? "true" : "false");
        PrintJsonStrings(out, t.names);
        std::fprintf(out, "]}");
    }
    std::fprintf(out, "\n  ],\n  \"crosses\": [");
    for (std::size_t i = 0; i < catalog.Crosses().size(); ++i) {
        const auto& t = catalog.Crosses()[i];
        const auto& s = t.spec;
        std::fprintf(out, "%s\n    {\"name\": %s, \"plugin\": %s, \"every\": %" PRId64 ", \"fill_gaps\": %s, \"columns\": [",
                     i ? "," : "", JsonString(s.name).c_str(),
                     t.plugin ? JsonString(catalog.Plugins()[*t.plugin].name).c_str() : "null",
                     s.bars.every, s.bars.fill_gaps ? "true" : "false");
        PrintJsonStrings(out, t.names);
        std::fprintf(out, "]}");
    }
    std::fprintf(out, "\n  ]\n}\n");
}

void PrintFeedReport(std::FILE* out, const PipelineTable<nq_mbo>& table, const RunResult& result) {
    PrintFeedSummary(out, table, result);

    Orderbook::Stats total;
    table.ForEachInstrument([&](const auto& e) {
        Accumulate(total, e.pipeline->Book().GetStats());
    });
    std::fprintf(out, "book  ");
    PrintBookStats(out, total);
    std::fprintf(out, "\n");

    table.ForEachInstrument([&](const auto& e) {
        const Orderbook& book = e.pipeline->Book();
        PrintInstrumentSummary(out, e);
        std::fprintf(out, " orders=%zu bid_levels=%zu ask_levels=%zu  ", book.OrderCount(), book.BidLevelCount(),
                     book.AskLevelCount());
        PrintBookStats(out, book.GetStats());
        std::fprintf(out, "\n");
    });
}

void PrintFeedReport(std::FILE* out, const PipelineTable<nq_ohlcv>& table, const RunResult& result) {
    PrintFeedSummary(out, table, result);

    table.ForEachInstrument([&](const auto& e) {
        PrintInstrumentSummary(out, e);
        std::fprintf(out, "\n");
    });
}

}  // namespace nq
