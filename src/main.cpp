#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <CLI/CLI.hpp>

#include <Config.h>
#include <Pipeline.h>

namespace {

enum ExitCode : int {
    kOk = 0,
    kPluginLoad = 2,
    kAbiMismatch = 3,
    kPluginInit = 4,
    kSamplerInvalid = 5,
    kParamsInvalid = 6,
};

struct CheckOptions {
    std::string symbol;
    std::int64_t tick_size = 1;
    std::int64_t lot_size = 1;
    std::vector<std::string> plugins;
    std::string plugin_dir;
    std::vector<std::string> samplers;
    bool fill_gaps = false;
    std::string format = "text";
};

const char* RequiresName(std::uint8_t r) {
    switch (r) {
        case NQ_REQUIRES_L1: return "L1";
        case NQ_REQUIRES_MBP: return "MBP";
        case NQ_REQUIRES_MBO: return "MBO";
    }
    return "?";
}

const char* KindName(std::uint8_t k) { return k == NQ_COLUMN_CUMULATIVE ? "cumulative" : "snapshot"; }

const char* TriggerName(nq::Trigger t) {
    switch (t) {
        case nq::Trigger::Time: return "time";
        case nq::Trigger::Events: return "events";
        case nq::Trigger::Trades: return "trades";
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

void PrintText(const nq::FeatureEngine& eng, const std::vector<nq::PluginFile>& files) {
    const auto& inst = eng.Instrument();
    std::printf("instrument  %s  tick_size=%lld  lot_size=%lld  price_scale=%lld  qty_scale=%lld\n",
                inst.symbol.c_str(), static_cast<long long>(inst.tick_size),
                static_cast<long long>(inst.lot_size), static_cast<long long>(nlib::price_scale),
                static_cast<long long>(nlib::qty_scale));

    std::printf("plugins (%zu)\n", eng.PluginCount());
    for (std::size_t i = 0; i < eng.PluginCount(); ++i) {
        const auto& p = eng.Plugin(i);
        std::printf("  %-16s %s  sdk=%s requires=%s cols=%u", files[i].name.c_str(), p.Path().c_str(),
                    p.Schema().sdk_version, RequiresName(p.Schema().requirement), p.Schema().ncol);
        for (const auto& [k, v] : files[i].params) std::printf("  %s=%s", k.c_str(), v.c_str());
        std::printf("\n");
    }

    std::printf("columns (%zu)\n", eng.Columns().size());
    for (const auto& c : eng.Columns()) {
        std::printf("  %-32s %-10s %-10s %s\n", c.name.c_str(), c.unit.c_str(), KindName(c.kind), c.doc.c_str());
    }

    std::printf("samplers (%zu)\n", eng.SamplerCount());
    for (std::size_t i = 0; i < eng.SamplerCount(); ++i) {
        const auto& s = eng.GetSampler(i).Spec();
        std::printf("  %-16s %s every=%lld%s fill_gaps=%s cols=%zu\n", s.name.c_str(), TriggerName(s.trigger),
                    static_cast<long long>(s.every), s.trigger == nq::Trigger::Time ? "ns" : "",
                    s.fill_gaps ? "yes" : "no", eng.GetSampler(i).Ncol());
    }
}

void PrintJson(const nq::FeatureEngine& eng, const std::vector<nq::PluginFile>& files) {
    const auto& inst = eng.Instrument();
    std::printf("{\n  \"instrument\": {\"symbol\": %s, \"tick_size\": %lld, \"lot_size\": %lld, "
                "\"price_scale\": %lld, \"qty_scale\": %lld},\n",
                JsonString(inst.symbol).c_str(), static_cast<long long>(inst.tick_size),
                static_cast<long long>(inst.lot_size), static_cast<long long>(nlib::price_scale),
                static_cast<long long>(nlib::qty_scale));

    std::printf("  \"plugins\": [");
    for (std::size_t i = 0; i < eng.PluginCount(); ++i) {
        const auto& p = eng.Plugin(i);
        std::printf("%s\n    {\"name\": %s, \"path\": %s, \"sdk_version\": %s, \"requires\": \"%s\", \"ncol\": %u, \"params\": {",
                    i ? "," : "", JsonString(files[i].name).c_str(), JsonString(p.Path()).c_str(),
                    JsonString(p.Schema().sdk_version).c_str(), RequiresName(p.Schema().requirement),
                    p.Schema().ncol);
        for (std::size_t j = 0; j < files[i].params.size(); ++j) {
            std::printf("%s%s: %s", j ? ", " : "", JsonString(files[i].params[j].first).c_str(),
                        JsonString(files[i].params[j].second).c_str());
        }
        std::printf("}}");
    }
    std::printf("\n  ],\n");

    std::printf("  \"columns\": [");
    for (std::size_t i = 0; i < eng.Columns().size(); ++i) {
        const auto& c = eng.Columns()[i];
        std::printf("%s\n    {\"name\": %s, \"unit\": %s, \"kind\": \"%s\", \"doc\": %s, \"plugin\": %s}",
                    i ? "," : "", JsonString(c.name).c_str(), JsonString(c.unit).c_str(), KindName(c.kind),
                    JsonString(c.doc).c_str(), JsonString(files[c.plugin_index].name).c_str());
    }
    std::printf("\n  ],\n");

    std::printf("  \"samplers\": [");
    for (std::size_t i = 0; i < eng.SamplerCount(); ++i) {
        const auto& sampler = eng.GetSampler(i);
        const auto& s = sampler.Spec();
        std::printf("%s\n    {\"name\": %s, \"trigger\": \"%s\", \"every\": %lld, \"fill_gaps\": %s, \"columns\": [",
                    i ? "," : "", JsonString(s.name).c_str(), TriggerName(s.trigger),
                    static_cast<long long>(s.every), s.fill_gaps ? "true" : "false");
        const auto& cols = sampler.Rows().Columns();
        for (std::size_t j = 0; j < cols.size(); ++j) std::printf("%s%s", j ? ", " : "", JsonString(cols[j]).c_str());
        std::printf("]}");
    }
    std::printf("\n  ]\n}\n");
}

int Check(const CheckOptions& opt) {
    std::vector<nq::SamplerSpec> specs;
    try {
        for (const auto& s : opt.samplers) {
            nq::SamplerSpec spec = nq::ParseSamplerSpec(s);
            spec.fill_gaps = opt.fill_gaps && spec.trigger == nq::Trigger::Time;
            specs.push_back(std::move(spec));
        }
    } catch (const std::invalid_argument& e) {
        std::fprintf(stderr, "nqbook: %s\n", e.what());
        return kSamplerInvalid;
    }

    std::vector<nq::PluginFile> files;
    try {
        for (const auto& p : opt.plugins) files.push_back(nq::DescribePluginFile(p));
        if (!opt.plugin_dir.empty()) {
            for (auto& f : nq::ScanPluginDir(opt.plugin_dir)) files.push_back(std::move(f));
        }
    } catch (const std::invalid_argument& e) {
        std::fprintf(stderr, "nqbook: %s\n", e.what());
        return kParamsInvalid;
    }

    nq::Pipeline pipe(nq::InstrumentSpec{opt.symbol, opt.tick_size, opt.lot_size});
    nq::FeatureEngine& eng = pipe.Engine();
    try {
        for (const auto& f : files) eng.LoadPlugin(f.path.string(), f.name, f.params);
    } catch (const nq::host::AbiMismatchError& e) {
        std::fprintf(stderr, "nqbook: %s\n", e.what());
        return kAbiMismatch;
    } catch (const nq::host::PluginInitError& e) {
        std::fprintf(stderr, "nqbook: %s\n", e.what());
        return kPluginInit;
    } catch (const nq::host::PluginLoadError& e) {
        std::fprintf(stderr, "nqbook: %s\n", e.what());
        return kPluginLoad;
    } catch (const std::invalid_argument& e) {
        std::fprintf(stderr, "nqbook: %s\n", e.what());
        return kPluginLoad;
    }

    try {
        for (auto& spec : specs) eng.AddSampler(std::move(spec));
    } catch (const std::invalid_argument& e) {
        std::fprintf(stderr, "nqbook: %s\n", e.what());
        return kSamplerInvalid;
    }

    if (opt.format == "json") PrintJson(eng, files);
    else PrintText(eng, files);
    return kOk;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"nqbook: MBO order book with a plugin feature engine"};
    app.require_subcommand(1);

    CheckOptions opt;
    CLI::App* check = app.add_subcommand("check", "Load plugins and samplers, print what would run, exit");
    check->add_option("--symbol", opt.symbol, "Instrument symbol")->required();
    check->add_option("--tick-size", opt.tick_size, "Price tick in 1/price_scale units")->check(CLI::PositiveNumber);
    check->add_option("--lot-size", opt.lot_size, "Quantity lot in 1/qty_scale units")->check(CLI::PositiveNumber);
    check->add_option("--plugin", opt.plugins, "Plugin .so to load, in order; repeatable")->check(CLI::ExistingFile);
    check->add_option("--plugin-dir", opt.plugin_dir, "Directory whose *.so are loaded in filename order")->check(CLI::ExistingDirectory);
    check->add_option("--sampler", opt.samplers, "NAME=TRIGGER:EVERY, e.g. 1s=time:1s, fast=events:100, t=trades:1; repeatable");
    check->add_flag("--fill-gaps", opt.fill_gaps, "Time samplers emit a row for every empty bar");
    check->add_option("--format", opt.format, "Output format")->check(CLI::IsMember({"text", "json"}));

    std::string help_topic;
    CLI::App* help = app.add_subcommand("help", "Print help for nqbook or for a subcommand");
    help->add_option("subcommand", help_topic, "Subcommand to describe");

    CLI11_PARSE(app, argc, argv);

    if (help->parsed()) {
        if (help_topic.empty()) {
            std::printf("%s", app.get_formatter()->make_help(&app, app.get_name(), CLI::AppFormatMode::Normal).c_str());
            return kOk;
        }
        try {
            std::printf("%s", app.get_subcommand(help_topic)->help(app.get_name()).c_str());
            return kOk;
        } catch (const CLI::OptionNotFound&) {
            std::fprintf(stderr, "nqbook: unknown subcommand '%s'\n", help_topic.c_str());
            return static_cast<int>(CLI::ExitCodes::OptionNotFound);
        }
    }
    if (check->parsed()) return Check(opt);
    return kOk;
}
