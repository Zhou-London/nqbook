#include <atomic>
#include <csignal>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <nq/params.hpp>

#include <Config.h>
#include <Defaults.h>
#include <Feature/FeatureCatalog.h>
#include <Report.h>
#include <Session.h>
#include <Writer/CsvWriter.h>

namespace {

enum ExitCode : int {
    kOk = 0,
    kPluginLoad = 2,
    kAbiMismatch = 3,
    kPluginInit = 4,
    kSamplerInvalid = 5,
    kParamsInvalid = 6,
    kAdapterFailed = 7,
    kOutputFailed = 8,
    kInterrupted = 130,
};

std::atomic<bool> g_stop{false};

void OnSignal(int) { g_stop.store(true, std::memory_order_relaxed); }

struct PluginOptions {
    std::vector<std::string> plugins;
    std::string plugin_dir;
    std::vector<std::string> samplers;
    bool fill_gaps = false;
};

struct AdapterOptions {
    std::string adapter;
    std::vector<std::string> params;
    std::size_t queue_capacity = nq::defaults::kQueueCapacity;
};

void AddPluginOptions(CLI::App* cmd, PluginOptions& opt) {
    cmd->add_option("--plugin", opt.plugins, "Plugin .so to load, in order; repeatable")->check(CLI::ExistingFile);
    cmd->add_option("--plugin-dir", opt.plugin_dir, "Directory whose *.so are loaded in filename order")->check(CLI::ExistingDirectory);
    cmd->add_option("--sampler", opt.samplers, "NAME=TRIGGER:EVERY, e.g. 1s=time:1s, fast=events:100, t=trades:1; repeatable");
    cmd->add_flag("--fill-gaps", opt.fill_gaps, "Time samplers emit a row for every empty bar");
}

void AddAdapterOptions(CLI::App* cmd, AdapterOptions& opt) {
    cmd->add_option("--adapter", opt.adapter, "Adapter .so that produces the mbo stream")->required()->check(CLI::ExistingFile);
    cmd->add_option("--adapter-param", opt.params, "KEY=VALUE for the adapter, after its .params file; repeatable");
    cmd->add_option("--queue-capacity", opt.queue_capacity, "Adapter-to-book queue capacity, rounded up to a power of two")->check(CLI::PositiveNumber);
}

int ExitCodeFor(const std::exception& e, int fallback) {
    if (dynamic_cast<const nq::host::AbiMismatchError*>(&e)) return kAbiMismatch;
    if (dynamic_cast<const nq::host::PluginInitError*>(&e)) return kPluginInit;
    if (dynamic_cast<const nq::host::PluginLoadError*>(&e)) return kPluginLoad;
    if (dynamic_cast<const nq::host::PluginError*>(&e)) return kAdapterFailed;
    return fallback;
}

template <typename Step>
int Attempt(int fallback, Step&& step) {
    try {
        step();
        return kOk;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nqbook: %s\n", e.what());
        return ExitCodeFor(e, fallback);
    }
}

int LoadCatalog(const PluginOptions& opt, nq::FeatureCatalog& catalog) {
    std::vector<nq::SamplerSpec> specs;
    if (int rc = Attempt(kSamplerInvalid, [&] {
            for (const auto& s : opt.samplers) {
                nq::SamplerSpec spec = nq::ParseSamplerSpec(s);
                spec.fill_gaps = opt.fill_gaps && spec.trigger == nq::Trigger::Time;
                specs.push_back(std::move(spec));
            }
        })) return rc;

    std::vector<nq::PluginFile> files;
    if (int rc = Attempt(kParamsInvalid, [&] {
            for (const auto& p : opt.plugins) files.push_back(nq::DescribePluginFile(p));
            if (!opt.plugin_dir.empty()) {
                for (auto& f : nq::ScanPluginDir(opt.plugin_dir)) files.push_back(std::move(f));
            }
        })) return rc;

    if (int rc = Attempt(kPluginLoad, [&] {
            for (auto& f : files) catalog.LoadPlugin(f.path.string(), f.name, std::move(f.params));
        })) return rc;

    return Attempt(kSamplerInvalid, [&] {
        for (auto& spec : specs) catalog.AddSampler(std::move(spec));
    });
}

int ParseAdapterParams(const AdapterOptions& opt, nq::ParamsBuilder& pb) {
    pb.PriceScale(NQ_PRICE_SCALE).QtyScale(NQ_QTY_SCALE);
    return Attempt(kParamsInvalid, [&] {
        nq::PluginFile f = nq::DescribePluginFile(opt.adapter);
        for (const auto& [k, v] : f.params) pb.Set(k, v);
        for (const auto& kv : opt.params) {
            const auto eq = kv.find('=');
            if (eq == std::string::npos || eq == 0) throw std::invalid_argument("--adapter-param expects KEY=VALUE: '" + kv + "'");
            pb.Set(kv.substr(0, eq), kv.substr(eq + 1));
        }
    });
}

int Check(const PluginOptions& popt, const std::string& format) {
    nq::FeatureCatalog catalog;
    if (int rc = LoadCatalog(popt, catalog)) return rc;

    if (int rc = Attempt(kPluginInit, [&] { nq::Pipeline probe(catalog, nq::Instrument{0, "probe", 1, 1}, 16); })) return rc;

    if (format == "json") nq::PrintCatalogJson(stdout, catalog);
    else nq::PrintCatalogText(stdout, catalog);
    return kOk;
}

int Run(const PluginOptions& popt, const AdapterOptions& aopt, const std::string& out_dir, bool feed_only) {
    nq::FeatureCatalog catalog;
    if (!feed_only) {
        if (int rc = LoadCatalog(popt, catalog)) return rc;
    }

    nq::ParamsBuilder pb;
    if (int rc = ParseAdapterParams(aopt, pb)) return rc;

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    std::optional<nq::host::AdapterPlugin> adapter;
    if (int rc = Attempt(kPluginLoad, [&] { adapter.emplace(aopt.adapter); })) return rc;

    std::optional<nq::Session> session;
    nq::RunResult result;
    if (int rc = Attempt(kAdapterFailed, [&] {
            session.emplace(catalog, *adapter, *pb.Build(), aopt.queue_capacity);
            result = session->Run(g_stop);
        })) return rc;
    nq::PrintFeedReport(stderr, session->Pipelines(), result);

    if (!feed_only && !out_dir.empty()) {
        if (int rc = Attempt(kOutputFailed, [&] {
                for (const auto& p : nq::WriteSamplerCsv(session->Pipelines(), out_dir)) {
                    std::fprintf(stderr, "wrote %s\n", p.string().c_str());
                }
            })) return rc;
    }

    if (result.interrupted) return kInterrupted;
    if (result.adapter_status != 0) {
        std::fprintf(stderr, "nqbook: adapter ended with status %d\n", result.adapter_status);
        return kAdapterFailed;
    }
    return kOk;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"nqbook: MBO order book with a plugin feature engine"};
    app.require_subcommand(1);

    PluginOptions check_plugins;
    std::string check_format = "text";
    CLI::App* check = app.add_subcommand("check", "Load plugins and samplers, print what would run, exit");
    AddPluginOptions(check, check_plugins);
    check->add_option("--format", check_format, "Output format")->check(CLI::IsMember({"text", "json"}));

    PluginOptions run_plugins;
    AdapterOptions run_adapter;
    std::string out_dir;
    CLI::App* run = app.add_subcommand("run", "Consume an adapter's mbo stream through the book and plugins");
    AddAdapterOptions(run, run_adapter);
    AddPluginOptions(run, run_plugins);
    run->add_option("--out-dir", out_dir, "Directory that receives one CSV per sampler when the run ends");

    AdapterOptions feed_adapter;
    CLI::App* feed = app.add_subcommand("check-feed", "Consume an adapter's mbo stream through bare books and report feed quality");
    AddAdapterOptions(feed, feed_adapter);

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
    if (check->parsed()) return Check(check_plugins, check_format);
    if (run->parsed()) return Run(run_plugins, run_adapter, out_dir, false);
    if (feed->parsed()) return Run(PluginOptions{}, feed_adapter, "", true);
    return kOk;
}
