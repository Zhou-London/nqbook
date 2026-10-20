#include <Report.h>

#include <string_view>

namespace nq {

namespace {

const char* RequiresName(std::uint8_t r) {
    switch (r) {
        case NQ_REQUIRES_L1: return "L1";
        case NQ_REQUIRES_MBP: return "MBP";
        case NQ_REQUIRES_MBO: return "MBO";
    }
    return "?";
}

const char* KindName(std::uint8_t k) { return k == NQ_COLUMN_CUMULATIVE ? "cumulative" : "snapshot"; }

const char* TriggerName(Trigger t) {
    switch (t) {
        case Trigger::Time: return "time";
        case Trigger::Events: return "events";
        case Trigger::Trades: return "trades";
    }
    return "?";
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

}  // namespace

void PrintCatalogText(std::FILE* out, const FeatureCatalog& catalog) {
    std::fprintf(out, "plugins (%zu)\n", catalog.PluginCount());
    for (std::size_t i = 0; i < catalog.PluginCount(); ++i) {
        const auto& p = catalog.Plugin(i);
        std::fprintf(out, "  %-16s %s  sdk=%s requires=%s cols=%u", catalog.PluginName(i).c_str(), p.Path().c_str(),
                     p.Schema().sdk_version, RequiresName(p.Schema().requirement), p.Schema().ncol);
        for (const auto& [k, v] : catalog.PluginParams(i)) std::fprintf(out, "  %s=%s", k.c_str(), v.c_str());
        std::fprintf(out, "\n");
    }

    std::fprintf(out, "columns (%zu)\n", catalog.Columns().size());
    for (const auto& c : catalog.Columns()) {
        std::fprintf(out, "  %-32s %-10s %-10s %s\n", c.name.c_str(), c.unit.c_str(), KindName(c.kind), c.doc.c_str());
    }

    std::fprintf(out, "samplers (%zu)\n", catalog.Samplers().size());
    for (const auto& t : catalog.Samplers()) {
        const auto& s = t.spec;
        std::fprintf(out, "  %-16s %s every=%lld%s fill_gaps=%s cols=%zu\n", s.name.c_str(), TriggerName(s.trigger),
                     static_cast<long long>(s.every), s.trigger == Trigger::Time ? "ns" : "",
                     s.fill_gaps ? "yes" : "no", t.columns.size());
    }
}

void PrintCatalogJson(std::FILE* out, const FeatureCatalog& catalog) {
    std::fprintf(out, "{\n  \"plugins\": [");
    for (std::size_t i = 0; i < catalog.PluginCount(); ++i) {
        const auto& p = catalog.Plugin(i);
        std::fprintf(out, "%s\n    {\"name\": %s, \"path\": %s, \"sdk_version\": %s, \"requires\": \"%s\", \"ncol\": %u, \"params\": {",
                     i ? "," : "", JsonString(catalog.PluginName(i)).c_str(), JsonString(p.Path()).c_str(),
                     JsonString(p.Schema().sdk_version).c_str(), RequiresName(p.Schema().requirement),
                     p.Schema().ncol);
        const auto& params = catalog.PluginParams(i);
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
                     JsonString(c.doc).c_str(), JsonString(catalog.PluginName(c.plugin_index)).c_str());
    }
    std::fprintf(out, "\n  ],\n  \"samplers\": [");
    for (std::size_t i = 0; i < catalog.Samplers().size(); ++i) {
        const auto& t = catalog.Samplers()[i];
        const auto& s = t.spec;
        std::fprintf(out, "%s\n    {\"name\": %s, \"trigger\": \"%s\", \"every\": %lld, \"fill_gaps\": %s, \"columns\": [",
                     i ? "," : "", JsonString(s.name).c_str(), TriggerName(s.trigger),
                     static_cast<long long>(s.every), s.fill_gaps ? "true" : "false");
        for (std::size_t j = 0; j < t.names.size(); ++j) {
            std::fprintf(out, "%s%s", j ? ", " : "", JsonString(t.names[j]).c_str());
        }
        std::fprintf(out, "]}");
    }
    std::fprintf(out, "\n  ]\n}\n");
}

void PrintFeedReport(std::FILE* out, const PipelineTable& table, const RunResult& result) {
    const FeedStats& f = table.Stats();
    std::fprintf(out, "feed  events=%llu instruments=%zu ts_regressions=%llu instrument_unknown=%llu stalls=%llu first_ts=%lld last_ts=%lld adapter_status=%d%s\n",
                 static_cast<unsigned long long>(f.events), table.InstrumentCount(),
                 static_cast<unsigned long long>(f.ts_regressions),
                 static_cast<unsigned long long>(f.instrument_unknown),
                 static_cast<unsigned long long>(result.stalls), static_cast<long long>(f.first_ts),
                 static_cast<long long>(f.last_ts), result.adapter_status, result.interrupted ? " interrupted" : "");

    Orderbook::Stats total;
    for (const auto& e : table.Entries()) {
        if (!e.pipeline) continue;
        const Orderbook::Stats& s = e.pipeline->Book().GetStats();
        total.adds += s.adds;
        total.cancels += s.cancels;
        total.modifies += s.modifies;
        total.trades += s.trades;
        total.clears += s.clears;
        total.add_duplicate += s.add_duplicate;
        total.cancel_unknown += s.cancel_unknown;
        total.modify_unknown += s.modify_unknown;
        total.trade_unknown += s.trade_unknown;
        total.trade_over_order += s.trade_over_order;
        total.trade_over_level += s.trade_over_level;
        total.qty_invalid += s.qty_invalid;
    }
    auto print_stats = [&](const Orderbook::Stats& s) {
        std::fprintf(out, "adds=%llu cancels=%llu modifies=%llu trades=%llu clears=%llu",
                     static_cast<unsigned long long>(s.adds), static_cast<unsigned long long>(s.cancels),
                     static_cast<unsigned long long>(s.modifies), static_cast<unsigned long long>(s.trades),
                     static_cast<unsigned long long>(s.clears));
        std::fprintf(out, " | add_duplicate=%llu cancel_unknown=%llu modify_unknown=%llu trade_unknown=%llu trade_over_order=%llu trade_over_level=%llu qty_invalid=%llu",
                     static_cast<unsigned long long>(s.add_duplicate),
                     static_cast<unsigned long long>(s.cancel_unknown),
                     static_cast<unsigned long long>(s.modify_unknown),
                     static_cast<unsigned long long>(s.trade_unknown),
                     static_cast<unsigned long long>(s.trade_over_order),
                     static_cast<unsigned long long>(s.trade_over_level),
                     static_cast<unsigned long long>(s.qty_invalid));
    };
    std::fprintf(out, "book  ");
    print_stats(total);
    std::fprintf(out, "\n");

    for (const auto& e : table.Entries()) {
        if (!e.pipeline) continue;
        const auto& spec = e.pipeline->Engine().GetInstrument();
        const Orderbook& book = e.pipeline->Book();
        std::fprintf(out, "  %-6u %-16s %s events=%llu ts_regressions=%llu orders=%zu bid_levels=%zu ask_levels=%zu  ",
                     e.id, spec.symbol.c_str(), e.stats.resolved ? "" : "(unresolved)",
                     static_cast<unsigned long long>(e.stats.events),
                     static_cast<unsigned long long>(e.stats.ts_regressions), book.OrderCount(),
                     book.BidLevelCount(), book.AskLevelCount());
        print_stats(book.GetStats());
        std::fprintf(out, "\n");
    }
}

}  // namespace nq
