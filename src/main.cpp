#include <atomic>
#include <concepts>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
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
#include <Writer/ParquetWriter.h>

namespace {

/* ---------- Exit Code Handling ---------- */
enum ExitCode : int {
    kOk = 0,
    kPluginLoad = 2,
    kAbiMismatch = 3,
    kPluginInit = 4,
    kSamplerInvalid = 5,
    kParamsInvalid = 6,
    kAdapterFailed = 7,
    kOutputFailed = 8,
    kFeedMismatch = 9,
    kInterrupted = 130,
};

int ExitCodeFor(const std::exception& e, int fallback) {
    if (dynamic_cast<const nq::FeedMismatchError*>(&e)) return kFeedMismatch;
    if (dynamic_cast<const nq::host::AbiMismatchError*>(&e)) return kAbiMismatch;
    if (dynamic_cast<const nq::host::PluginInitError*>(&e)) return kPluginInit;
    if (dynamic_cast<const nq::host::PluginLoadError*>(&e)) return kPluginLoad;
    if (dynamic_cast<const nq::host::PluginError*>(&e)) return kAdapterFailed;
    return fallback;
}

int Attempt(int fallback, std::invocable auto&& step) {
    try {
        step();
        return kOk;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nqhpc: %s\n", e.what());
        return ExitCodeFor(e, fallback);
    }
}

/* ---------- CLI Options ---------- */
struct PluginOptions {
    std::vector<std::string> plugins;
    std::string plugin_dir;
    std::vector<std::string> samplers;
    std::vector<std::string> crosses;
    bool fill_gaps = false;
};

struct AdapterOptions {
    std::string adapter;
    std::vector<std::string> params;
    std::size_t queue_capacity = nq::defaults::kQueueCapacity;
};

struct OutputOptions {
    std::string dir{nq::defaults::kOutDir};
    std::string format = "parquet";
    std::int64_t row_group_rows = nq::defaults::kRowGroupRows;
};

void AddPluginOptions(CLI::App* cmd, PluginOptions& opt) {
    cmd->add_option("--plugin", opt.plugins, "Plugin .so to load, in order; repeatable")->check(CLI::ExistingFile);
    cmd->add_option("--plugin-dir", opt.plugin_dir, "Directory whose *.so are loaded in filename order")->check(CLI::ExistingDirectory);
    cmd->add_option("--sampler", opt.samplers, "NAME=TRIGGER:EVERY, e.g. 1s=time:1s, fast=events:100, t=trades:1 (trades needs an MBO feed); repeatable");
    cmd->add_option("--cross", opt.crosses, "NAME=PLUGIN:EVERY, e.g. x=example_cross_features:1s, runs that plugin's cross on a time grid; repeatable");
    cmd->add_flag("--fill-gaps", opt.fill_gaps, "Time samplers and crosses emit rows for every empty bar");
}

void AddAdapterOptions(CLI::App* cmd, AdapterOptions& opt) {
    cmd->add_option("--adapter", opt.adapter, "Adapter .so that produces the MBO or OHLCV feed")->required()->check(CLI::ExistingFile);
    cmd->add_option("--adapter-param", opt.params, "KEY=VALUE for the adapter, after its .params file; repeatable");
    cmd->add_option("--queue-capacity", opt.queue_capacity, "Adapter-to-pipeline queue capacity, rounded up to a power of two")->check(CLI::PositiveNumber);
}

void AddOutputOptions(CLI::App* cmd, OutputOptions& opt) {
    cmd->add_option("--out-dir", opt.dir, "Directory that receives one file per sampler plus instruments.parquet")->capture_default_str();
    cmd->add_option("--out-format", opt.format, "Output format")->check(CLI::IsMember({"parquet", "csv"}));
    cmd->add_option("--row-group", opt.row_group_rows, "Rows per Parquet row group")->check(CLI::PositiveNumber);
}

/* ---------- Pipeline Assembling ---------- */
int LoadCatalog(const PluginOptions& opt, nq::FeatureCatalog& catalog) {
    std::vector<nq::SamplerSpec> specs;
    std::vector<nq::CrossArg> crosses;
    if (int rc = Attempt(kSamplerInvalid, [&] {
            for (const auto& s : opt.samplers) specs.push_back(nq::ParseSamplerSpec(s, opt.fill_gaps));
            for (const auto& c : opt.crosses) crosses.push_back(nq::ParseCrossArg(c));
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
        for (auto& c : crosses) catalog.AddCross(std::move(c.name), c.plugin, c.every, opt.fill_gaps);
    });
}

/* ----------- Signal Handling ---------- */
std::atomic<bool> g_stop{false};

void OnSignal(int) { g_stop.store(true, std::memory_order_relaxed); }

/* ---------- Subcommands ---------- */
int Check(const PluginOptions& popt, const std::string& format) {
    nq::FeatureCatalog catalog;
    if (int rc = LoadCatalog(popt, catalog)) return rc;

    if (int rc = Attempt(kPluginInit, [&] {
            nq::FeatureEngine probe(catalog, nq::Instrument{0, "probe", 1, 1});
            for (const auto& c : catalog.Crosses()) c.spec.make();
        })) return rc;

    if (format == "json") nq::PrintCatalogJson(stdout, catalog);
    else nq::PrintCatalogText(stdout, catalog);
    return kOk;
}

template <class Record>
int RunSession(const nq::FeatureCatalog& catalog, const nq::host::AdapterPlugin& adapter, const nq_params& params,
               std::unique_ptr<nq::ParquetWriter> writer, std::size_t queue_capacity,
               const std::optional<std::filesystem::path>& csv_dir) {
    std::optional<nq::Session<Record>> session;
    nq::RunResult result;
    if (int rc = Attempt(kAdapterFailed, [&] {
            session.emplace(catalog, adapter, params, std::move(writer), queue_capacity);
            result = session->Run(g_stop);
        })) return rc;
    nq::PrintFeedReport(stderr, session->Pipelines(), result);

    if (result.writer) {
        if (!result.writer->error.empty()) {
            std::fprintf(stderr, "nqhpc: %s\n", result.writer->error.c_str());
            return kOutputFailed;
        }
        for (const auto& p : session->Writer()->Writer().Files()) std::fprintf(stderr, "wrote %s\n", p.string().c_str());
    }
    if (csv_dir) {
        if (int rc = Attempt(kOutputFailed, [&] {
                for (const auto& p : nq::WriteCsv(session->Pipelines(), *csv_dir)) {
                    std::fprintf(stderr, "wrote %s\n", p.string().c_str());
                }
            })) return rc;
    }

    if (result.interrupted) return kInterrupted;
    if (result.adapter_status != 0) {
        std::fprintf(stderr, "nqhpc: adapter ended with status %d\n", result.adapter_status);
        return kAdapterFailed;
    }
    return kOk;
}

int RunFeed(const nq::FeatureCatalog& catalog, const AdapterOptions& aopt, const OutputOptions* output) {
    nq::ParamsBuilder pb;
    if (int rc = Attempt(kParamsInvalid, [&] { pb = nq::ScaledParams(nq::AdapterParams(aopt.adapter, aopt.params)); })) return rc;

    std::optional<nq::host::AdapterPlugin> adapter;
    if (int rc = Attempt(kPluginLoad, [&] { adapter.emplace(aopt.adapter); })) return rc;

    const nq::Feed feed = nq::FeedOf(*adapter);
    if (int rc = Attempt(kFeedMismatch, [&] { catalog.RequireFeed(feed); })) return rc;

    std::unique_ptr<nq::ParquetWriter> writer;
    std::optional<std::filesystem::path> csv_dir;
    if (output && !output->dir.empty()) {
        if (output->format == "csv") {
            csv_dir = output->dir;
        } else if (int rc = Attempt(kOutputFailed, [&] {
                       writer = std::make_unique<nq::ParquetWriter>(catalog, output->dir, nq::ParquetOptions{output->row_group_rows});
                   })) {
            return rc;
        }
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    if (feed == nq::Feed::OHLCV) {
        return RunSession<nq_ohlcv>(catalog, *adapter, *pb.Build(), std::move(writer), aopt.queue_capacity, csv_dir);
    }
    return RunSession<nq_mbo>(catalog, *adapter, *pb.Build(), std::move(writer), aopt.queue_capacity, csv_dir);
}

int Run(const PluginOptions& popt, const AdapterOptions& aopt, const OutputOptions& oopt) {
    nq::FeatureCatalog catalog;
    if (int rc = LoadCatalog(popt, catalog)) return rc;
    return RunFeed(catalog, aopt, &oopt);
}

int CheckFeed(const AdapterOptions& aopt) { return RunFeed(nq::FeatureCatalog{}, aopt, nullptr); }

int PrintHelp(CLI::App& app, const std::string& topic) {
    if (topic.empty()) {
        std::printf("%s", app.get_formatter()->make_help(&app, app.get_name(), CLI::AppFormatMode::Normal).c_str());
        return kOk;
    }
    try {
        std::printf("%s", app.get_subcommand(topic)->help(app.get_name()).c_str());
        return kOk;
    } catch (const CLI::OptionNotFound&) {
        std::fprintf(stderr, "nqhpc: unknown subcommand '%s'\n", topic.c_str());
        return static_cast<int>(CLI::ExitCodes::OptionNotFound);
    }
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"nqhpc: plugin feature engine over an MBO or OHLCV feed"};
    app.require_subcommand(1);

    PluginOptions check_plugins;
    std::string check_format = "text";
    CLI::App* check = app.add_subcommand("check", "Load plugins and samplers, print what would run, exit");
    AddPluginOptions(check, check_plugins);
    check->add_option("--format", check_format, "Output format")->check(CLI::IsMember({"text", "json"}));

    PluginOptions run_plugins;
    AdapterOptions run_adapter;
    OutputOptions run_output;
    CLI::App* run = app.add_subcommand("run", "Consume an adapter's feed through the plugins and write the sampled rows");
    AddAdapterOptions(run, run_adapter);
    AddPluginOptions(run, run_plugins);
    AddOutputOptions(run, run_output);

    AdapterOptions feed_adapter;
    CLI::App* feed = app.add_subcommand("check-feed", "Consume an adapter's feed without plugins and report feed quality");
    AddAdapterOptions(feed, feed_adapter);

    std::string help_topic;
    CLI::App* help = app.add_subcommand("help", "Print help for nqhpc or for a subcommand");
    help->add_option("subcommand", help_topic, "Subcommand to describe");

    CLI11_PARSE(app, argc, argv);

    if (help->parsed()) return PrintHelp(app, help_topic);
    if (check->parsed()) return Check(check_plugins, check_format);
    if (run->parsed()) return Run(run_plugins, run_adapter, run_output);
    return CheckFeed(feed_adapter);
}
