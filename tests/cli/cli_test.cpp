#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Config.h>

#include "parquet_support.hpp"
#include "support.hpp"

namespace fs = std::filesystem;

static std::string Quote(const fs::path& p) { return "'" + p.string() + "'"; }

struct Result {
    int code;
    std::string out;
    std::string err;
};

static Result Run(const std::string& args, const fs::path& tmp, const fs::path& cwd = {}) {
    const fs::path out = tmp / "stdout.txt";
    const fs::path err = tmp / "stderr.txt";
    const std::string cd = cwd.empty() ? "" : "cd " + Quote(cwd) + " && ";
    const std::string cmd = cd + Quote(NQHPC_PATH) + " " + args + " >" + Quote(out) + " 2>" + Quote(err);
    const int status = std::system(cmd.c_str());
    auto slurp = [](const fs::path& p) {
        std::ifstream in(p);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    return Result{WIFEXITED(status) ? WEXITSTATUS(status) : -1, slurp(out), slurp(err)};
}

static bool Has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

static void WriteFile(const fs::path& path, std::string_view text) { std::ofstream(path) << text; }

static std::vector<std::string> Lines(const fs::path& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
}

static std::int64_t ParquetRows(const fs::path& path) { return nqtest::ReadParquet(path).table->num_rows(); }

static int TestParsing() {
    CHECK(nq::ParseDuration("1s") == 1'000'000'000);
    CHECK(nq::ParseDuration("250ms") == 250'000'000);
    CHECK(nq::ParseDuration("5m") == 300'000'000'000);
    CHECK(nq::ParseDuration("7ns") == 7);
    CHECK(nq::ParseDuration("2562047h") == 2562047 * 3'600'000'000'000);
    for (const char* too_large : {"2562048h", "9223372036854775808ns", "99999999999999999999s"}) {
        CHECK(nqtest::Throws<std::invalid_argument>([&] { nq::ParseDuration(too_large); }));
    }
    for (const char* bad : {"1", "s", "0s", "1x", "", "-1s"}) {
        CHECK(nqtest::Throws<std::invalid_argument>([&] { nq::ParseDuration(bad); }));
    }

    nq::SamplerSpec t = nq::ParseSamplerSpec("1s=time:1s");
    CHECK(t.name == "1s" && std::get<nq::TimeBars>(t.trigger).every == 1'000'000'000);
    nq::SamplerSpec e = nq::ParseSamplerSpec("fast=events:100");
    CHECK(e.name == "fast" && std::get<nq::EveryEvents>(e.trigger).every == 100);
    nq::SamplerSpec r = nq::ParseSamplerSpec("t=trades:1");
    CHECK(r.name == "t" && std::get<nq::EveryTrades>(r.trigger).every == 1);
    for (const char* bad : {"1s", "=time:1s", "a=time", "a=time:", "a=tick:1", "a=events:0", "a=events:1s", "a=time:5"}) {
        CHECK(nqtest::Throws<std::invalid_argument>([&] { nq::ParseSamplerSpec(bad); }));
    }
    return 0;
}

static int TestFiles(const fs::path& tmp) {
    const fs::path dir = tmp / "plugins";
    fs::create_directories(dir);
    fs::copy_file(NQ_EXAMPLE_FEATURES_PATH, dir / "b_test.so", fs::copy_options::overwrite_existing);
    fs::copy_file(NQ_EXAMPLE_FEATURES_PATH, dir / "a_test.so", fs::copy_options::overwrite_existing);
    WriteFile(dir / "a_test.params", "# comment\n\n  min_qty = 5 \nlabel=x=y\n");
    WriteFile(dir / "notes.txt", "ignored\n");

    auto files = nq::ScanPluginDir(dir);
    CHECK(files.size() == 2);
    CHECK(files[0].name == "a_test" && files[1].name == "b_test");
    CHECK(files[0].params.size() == 2);
    CHECK(files[0].params[0].first == "min_qty" && files[0].params[0].second == "5");
    CHECK(files[0].params[1].first == "label" && files[0].params[1].second == "x=y");
    CHECK(files[1].params.empty());

    WriteFile(dir / "b_test.params", "novalue\n");
    bool threw = false;
    try { nq::ScanPluginDir(dir); } catch (const std::invalid_argument& e) { threw = Has(e.what(), ":1:"); }
    CHECK(threw);
    fs::remove(dir / "b_test.params");
    return 0;
}

static int TestCli(const fs::path& tmp) {
    const fs::path dir = tmp / "plugins";
    Result r = Run("check --plugin-dir " + Quote(dir) + " --sampler 1s=time:1s --sampler t=trades:1 --fill-gaps", tmp);
    CHECK(r.code == 0);
    CHECK(Has(r.out, "plugins (2)") && Has(r.out, "columns (10)") && Has(r.out, "samplers (2)"));
    CHECK(Has(r.out, "a_test.spread") && Has(r.out, "b_test.trades") && Has(r.out, "min_qty=5"));
    CHECK(Has(r.out, "1s               time every=1000000000ns fill_gaps=yes cols=10"));
    CHECK(Has(r.out, "t                trades every=1 fill_gaps=no cols=10"));

    r = Run("check --plugin " + Quote(NQ_EXAMPLE_FEATURES_PATH) + " --format json", tmp);
    CHECK(r.code == 0);
    CHECK(Has(r.out, "\"name\": \"example_features\"") && Has(r.out, "\"kind\": \"cumulative\"") && Has(r.out, "\"samplers\": [\n  ]"));
    CHECK(Has(r.out, "\"crosses\": [\n  ]"));

    const std::string cross = "--plugin " + Quote(NQ_EXAMPLE_CROSS_FEATURES_PATH);
    r = Run("check " + cross + " --cross x=example_cross_features:1s --fill-gaps", tmp);
    CHECK(r.code == 0 && Has(r.out, "crosses (1)"));
    CHECK(Has(r.out, "x                example_cross_features every=1000000000ns fill_gaps=yes inputs=1 cols=4"));
    r = Run("check " + cross + " --cross x=example_cross_features:1s --format json", tmp);
    CHECK(r.code == 0 && Has(r.out, "\"plugin\": \"example_cross_features\"") && Has(r.out, "\"x.rank\""));
    r = Run("check " + cross + " --cross bad", tmp);
    CHECK(r.code == 5 && Has(r.err, "NAME=PLUGIN:EVERY"));
    r = Run("check " + cross + " --cross x=nope:1s", tmp);
    CHECK(r.code == 5 && Has(r.err, "unknown plugin"));
    r = Run("check --plugin " + Quote(NQ_EXAMPLE_FEATURES_PATH) + " --cross x=example_features:1s", tmp);
    CHECK(r.code == 5 && Has(r.err, "has no cross"));
    r = Run("check " + cross + " --sampler x=time:1s --cross x=example_cross_features:1s", tmp);
    CHECK(r.code == 5 && Has(r.err, "duplicate table name"));

    const std::string feed = "run --adapter " + Quote(NQ_SYNTHETIC_ADAPTER_PATH) + " --adapter-param count=60 --adapter-param instruments=3 ";
    const fs::path xcsv = tmp / "xcsv";
    r = Run(feed + cross + " --cross x=example_cross_features:10ns --out-format csv --out-dir " + Quote(xcsv), tmp);
    CHECK(r.code == 0 && fs::exists(xcsv / "x.csv"));
    CHECK(Lines(xcsv / "x.csv").at(0) == "instrument,symbol,ts,example_cross_features.mid,x.mean,x.z,x.rank");
    const fs::path xpq = tmp / "xpq";
    r = Run(feed + cross + " --cross x=example_cross_features:10ns --out-dir " + Quote(xpq), tmp);
    CHECK(r.code == 0 && !Has(r.err, "error=") && ParquetRows(xpq / "x.parquet") == 19);

    r = Run("check --plugin " + Quote(NQ_FAKE_PLUGIN_PATH), tmp);
    CHECK(r.code == 3 && Has(r.err, "abi hash mismatch"));

    r = Run("check --plugin " + Quote(dir / "notes.txt"), tmp);
    CHECK(r.code == 2 && Has(r.err, "dlopen failed"));

    WriteFile(dir / "b_test.params", "min_qty=abc\n");
    r = Run("check --plugin-dir " + Quote(dir), tmp);
    CHECK(r.code == 4 && Has(r.err, "nq_init failed"));
    WriteFile(dir / "b_test.params", "broken line\n");
    r = Run("check --plugin-dir " + Quote(dir), tmp);
    CHECK(r.code == 6 && Has(r.err, "expected key=value"));
    fs::remove(dir / "b_test.params");

    r = Run("check --sampler bad", tmp);
    CHECK(r.code == 5);
    r = Run("check --sampler a=time:1s --sampler a=time:2s", tmp);
    CHECK(r.code == 5 && Has(r.err, "duplicate table name"));

    r = Run("check --plugin-dir " + Quote(tmp / "missing"), tmp);
    CHECK(r.code >= 100);
    r = Run("", tmp);
    CHECK(r.code >= 100);
    r = Run("check --help", tmp);
    CHECK(r.code == 0 && Has(r.out, "--plugin-dir"));
    r = Run("help", tmp);
    CHECK(r.code == 0 && Has(r.out, "SUBCOMMANDS") && Has(r.out, "check") && Has(r.out, "run") && Has(r.out, "check-feed"));
    r = Run("help check", tmp);
    CHECK(r.code == 0 && Has(r.out, "--plugin-dir"));
    r = Run("help nope", tmp);
    CHECK(r.code >= 100 && Has(r.err, "unknown subcommand"));
    return 0;
}

static int TestRunCli(const fs::path& tmp) {
    const std::string adapter = "--adapter " + Quote(NQ_SYNTHETIC_ADAPTER_PATH) + " --adapter-param count=10 --adapter-param instruments=2";
    const std::string plugin = " --plugin " + Quote(NQ_EXAMPLE_FEATURES_PATH) + " --sampler e=events:3 --sampler t=trades:1";

    const fs::path out = tmp / "out";
    Result r = Run("run " + adapter + plugin + " --out-format csv --out-dir " + Quote(out), tmp);
    CHECK(r.code == 0);
    CHECK(Has(r.err, "feed  events=10 instruments=2 ts_regressions=0 instrument_unknown=0"));
    CHECK(Has(r.err, "adapter_status=0") && Has(r.err, "1      SYN1") && Has(r.err, "2      SYN2"));
    CHECK(Has(r.err, "wrote ") && fs::exists(out / "e.csv") && fs::exists(out / "t.csv"));
    const auto e_csv = Lines(out / "e.csv");
    CHECK(e_csv.size() == 5);
    CHECK(e_csv[0] == "instrument,symbol,ts,example_features.spread,example_features.mid,example_features.buy_vol,example_features.sell_vol,example_features.trades");
    CHECK(e_csv[1].rfind("1,SYN1,5,", 0) == 0 && e_csv[2].rfind("1,SYN1,9,", 0) == 0);
    CHECK(e_csv[3].rfind("2,SYN2,6,", 0) == 0 && e_csv[4].rfind("2,SYN2,10,", 0) == 0);
    CHECK(Lines(out / "t.csv").size() == 1);

    const fs::path pq = tmp / "pq";
    r = Run("run " + adapter + plugin + " --row-group 2 --out-dir " + Quote(pq), tmp);
    CHECK(r.code == 0);
    CHECK(Has(r.err, "writer  rows=4 stalls=") && !Has(r.err, "error="));
    CHECK(fs::exists(pq / "e.parquet") && fs::exists(pq / "t.parquet") && fs::exists(pq / "instruments.parquet"));
    CHECK(Has(r.err, "wrote ") && Has(r.err, "instruments.parquet"));
    CHECK(ParquetRows(pq / "e.parquet") == 4 && ParquetRows(pq / "t.parquet") == 0 && ParquetRows(pq / "instruments.parquet") == 2);
    r = Run("run " + adapter + plugin + " --out-dir " + Quote(tmp / "plugins" / "notes.txt" / "x"), tmp);
    CHECK(r.code == 8);

    const fs::path cwd = tmp / "cwd";
    fs::create_directories(cwd);
    r = Run("run " + adapter + plugin, tmp, cwd);
    CHECK(r.code == 0 && Has(r.err, "wrote out/e.parquet"));
    CHECK(ParquetRows(cwd / "out" / "e.parquet") == 4 && ParquetRows(cwd / "out" / "instruments.parquet") == 2);
    r = Run("run --help", tmp);
    CHECK(r.code == 0 && Has(r.out, "[out]"));

    r = Run("check-feed " + adapter, tmp);
    CHECK(r.code == 0 && Has(r.err, "feed  events=10 instruments=2") && Has(r.err, "adds=10"));

    const fs::path adir = tmp / "adapter";
    fs::create_directories(adir);
    fs::copy_file(NQ_SYNTHETIC_ADAPTER_PATH, adir / "synthetic.so", fs::copy_options::overwrite_existing);
    WriteFile(adir / "synthetic.params", "count=7\n");
    r = Run("check-feed --adapter " + Quote(adir / "synthetic.so"), tmp);
    CHECK(r.code == 0 && Has(r.err, "events=7"));
    r = Run("check-feed --adapter " + Quote(adir / "synthetic.so") + " --adapter-param count=3", tmp);
    CHECK(r.code == 0 && Has(r.err, "events=3"));

    r = Run("check-feed --adapter " + Quote(NQ_SYNTHETIC_ADAPTER_PATH), tmp);
    CHECK(r.code == 4 && Has(r.err, "nq_adapter_init failed"));
    r = Run("check-feed " + adapter + " --adapter-param novalue", tmp);
    CHECK(r.code == 6);
    r = Run("check-feed --adapter " + Quote(NQ_EXAMPLE_FEATURES_PATH), tmp);
    CHECK(r.code == 2 && Has(r.err, "missing symbol"));
    r = Run("check-feed " + adapter + " --adapter-param fail_after=6", tmp);
    CHECK(r.code == 7 && Has(r.err, "events=6") && Has(r.err, "adapter ended with status -2"));
    return 0;
}

static int TestOhlcvCli(const fs::path& tmp) {
    const std::string adapter = "--adapter " + Quote(NQ_SYNTHETIC_OHLCV_ADAPTER_PATH) + " --adapter-param count=10 --adapter-param instruments=2";
    const std::string plugin = " --plugin " + Quote(NQ_EXAMPLE_OHLCV_FEATURES_PATH);
    const std::string samplers = " --sampler each=events:1 --sampler 1m=time:1m";
    const std::string mbo_adapter = "--adapter " + Quote(NQ_SYNTHETIC_ADAPTER_PATH) + " --adapter-param count=10";
    const std::string mbo_plugin = " --plugin " + Quote(NQ_EXAMPLE_FEATURES_PATH);

    const fs::path pq = tmp / "ohlcv_pq";
    Result r = Run("run " + adapter + plugin + samplers + " --out-dir " + Quote(pq), tmp);
    CHECK(r.code == 0);
    CHECK(Has(r.err, "feed  events=10 instruments=2 ts_regressions=0 instrument_unknown=0"));
    CHECK(Has(r.err, "1      SYN1") && Has(r.err, "2      SYN2") && !Has(r.err, "book  ") && !Has(r.err, "orders="));
    CHECK(ParquetRows(pq / "each.parquet") == 10 && ParquetRows(pq / "1m.parquet") == 10);
    CHECK(ParquetRows(pq / "instruments.parquet") == 2);

    const fs::path csv = tmp / "ohlcv_csv";
    r = Run("run " + adapter + plugin + samplers + " --out-format csv --out-dir " + Quote(csv), tmp);
    CHECK(r.code == 0);
    const auto each_csv = Lines(csv / "each.csv");
    CHECK(each_csv.size() >= 2);
    CHECK(each_csv[0] == "instrument,symbol,ts,example_ohlcv_features.close,example_ohlcv_features.range,example_ohlcv_features.volume,example_ohlcv_features.count");
    CHECK(each_csv[1] == "1,SYN1,0,101,3,1,1");

    r = Run("check" + plugin + samplers, tmp);
    CHECK(r.code == 0 && Has(r.out, "feed=OHLCV") && Has(r.out, "requires=none") && Has(r.out, "columns (4)"));
    r = Run("check" + mbo_plugin, tmp);
    CHECK(r.code == 0 && Has(r.out, "feed=MBO"));
    r = Run("check" + plugin + " --format json", tmp);
    CHECK(r.code == 0 && Has(r.out, "\"feed\": \"OHLCV\""));

    r = Run("check-feed " + adapter, tmp);
    CHECK(r.code == 0 && Has(r.err, "feed  events=10 instruments=2") && !Has(r.err, "adds="));

    r = Run("run " + adapter + mbo_plugin, tmp);
    CHECK(r.code == 9 && Has(r.err, "plugins read MBO, the adapter sends OHLCV"));
    r = Run("run " + mbo_adapter + plugin, tmp);
    CHECK(r.code == 9 && Has(r.err, "plugins read OHLCV, the adapter sends MBO"));
    r = Run("check" + mbo_plugin + plugin, tmp);
    CHECK(r.code == 9 && Has(r.err, "reads OHLCV, earlier plugins read MBO"));
    r = Run("run " + adapter + plugin + " --sampler t=trades:1", tmp);
    CHECK(r.code == 9 && Has(r.err, "trades sampler needs an MBO feed: t"));
    r = Run("run " + adapter + " --sampler t=trades:1", tmp);
    CHECK(r.code == 9 && Has(r.err, "trades sampler needs an MBO feed: t"));
    return 0;
}

int main() {
    const fs::path tmp = fs::temp_directory_path() / "nqhpc_cli_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    if (int rc = TestParsing()) return rc;
    if (int rc = TestFiles(tmp)) return rc;
    if (int rc = TestCli(tmp)) return rc;
    if (int rc = TestRunCli(tmp)) return rc;
    if (int rc = TestOhlcvCli(tmp)) return rc;
    fs::remove_all(tmp);
    std::printf("ok\n");
    return 0;
}
