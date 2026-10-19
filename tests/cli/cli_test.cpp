#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <Config.h>

#include "../../adapters/replay/mbo_file.hpp"

namespace fs = std::filesystem;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static std::string Quote(const fs::path& p) { return "'" + p.string() + "'"; }

struct Result {
    int code;
    std::string out;
    std::string err;
};

static Result Run(const std::string& args, const fs::path& tmp) {
    const fs::path out = tmp / "stdout.txt";
    const fs::path err = tmp / "stderr.txt";
    const std::string cmd = Quote(NQBOOK_PATH) + " " + args + " >" + Quote(out) + " 2>" + Quote(err);
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

static int TestParsing() {
    CHECK(nq::ParseDuration("1s") == 1'000'000'000);
    CHECK(nq::ParseDuration("250ms") == 250'000'000);
    CHECK(nq::ParseDuration("5m") == 300'000'000'000);
    CHECK(nq::ParseDuration("7ns") == 7);
    for (const char* bad : {"1", "s", "0s", "1x", "", "-1s"}) {
        bool threw = false;
        try { nq::ParseDuration(bad); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }

    nq::SamplerSpec t = nq::ParseSamplerSpec("1s=time:1s");
    CHECK(t.name == "1s" && t.trigger == nq::Trigger::Time && t.every == 1'000'000'000);
    nq::SamplerSpec e = nq::ParseSamplerSpec("fast=events:100");
    CHECK(e.name == "fast" && e.trigger == nq::Trigger::Events && e.every == 100);
    nq::SamplerSpec r = nq::ParseSamplerSpec("t=trades:1");
    CHECK(r.name == "t" && r.trigger == nq::Trigger::Trades && r.every == 1);
    for (const char* bad : {"1s", "=time:1s", "a=time", "a=time:", "a=tick:1", "a=events:0", "a=events:1s", "a=time:5"}) {
        bool threw = false;
        try { nq::ParseSamplerSpec(bad); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }
    return 0;
}

static int TestFiles(const fs::path& tmp) {
    const fs::path dir = tmp / "plugins";
    fs::create_directories(dir);
    fs::copy_file(NQ_TEST_PLUGIN_PATH, dir / "b_test.so", fs::copy_options::overwrite_existing);
    fs::copy_file(NQ_TEST_PLUGIN_PATH, dir / "a_test.so", fs::copy_options::overwrite_existing);
    {
        std::ofstream p(dir / "a_test.params");
        p << "# comment\n\n  min_qty = 5 \nlabel=x=y\n";
    }
    {
        std::ofstream junk(dir / "notes.txt");
        junk << "ignored\n";
    }

    auto files = nq::ScanPluginDir(dir);
    CHECK(files.size() == 2);
    CHECK(files[0].name == "a_test" && files[1].name == "b_test");
    CHECK(files[0].params.size() == 2);
    CHECK(files[0].params[0].first == "min_qty" && files[0].params[0].second == "5");
    CHECK(files[0].params[1].first == "label" && files[0].params[1].second == "x=y");
    CHECK(files[1].params.empty());

    {
        std::ofstream p(dir / "b_test.params");
        p << "novalue\n";
    }
    bool threw = false;
    try { nq::ScanPluginDir(dir); } catch (const std::invalid_argument& e) { threw = Has(e.what(), ":1:"); }
    CHECK(threw);
    fs::remove(dir / "b_test.params");
    return 0;
}

static int WriteFixture(const fs::path& path) {
    using nq::mbo_file::Writer;
    Writer w(path, {Writer::MakeInstrument(1, "AAA", 10, 1), Writer::MakeInstrument(2, "BBB", 5, 100)});
    const std::int64_t s = 1'000'000'000;
    w.Write(nq_mbo{s / 10, 1, 1005000000000, 700000000, 1, NQ_BID, NQ_ADD});
    w.Write(nq_mbo{s / 5, 1, 500000000000, 300000000, 2, NQ_BID, NQ_ADD});
    w.Write(nq_mbo{s / 2, 2, 1007000000000, 300000000, 1, NQ_ASK, NQ_ADD});
    w.Write(nq_mbo{s / 2 + 1, 2, 1007000000000, 200000000, 1, NQ_ASK, NQ_TRADE});
    w.Write(nq_mbo{s + s / 5, 3, 1004000000000, 100000000, 1, NQ_BID, NQ_ADD});
    w.Write(nq_mbo{s + s / 4, 2, 501000000000, 100000000, 2, NQ_ASK, NQ_ADD});
    w.Write(nq_mbo{2 * s + s / 10, 4, 1003000000000, 100000000, 1, NQ_BID, NQ_ADD});
    w.Close();
    return 0;
}

static int TestCli(const fs::path& tmp) {
    const fs::path dir = tmp / "plugins";
    const std::string base = "check ";

    Result r = Run(base + "--plugin-dir " + Quote(dir) + " --sampler 1s=time:1s --sampler t=trades:1 --fill-gaps", tmp);
    CHECK(r.code == 0);
    CHECK(Has(r.out, "plugins (2)") && Has(r.out, "columns (10)") && Has(r.out, "samplers (2)"));
    CHECK(Has(r.out, "a_test.spread") && Has(r.out, "b_test.trades") && Has(r.out, "min_qty=5"));
    CHECK(Has(r.out, "1s               time every=1000000000ns fill_gaps=yes cols=10"));
    CHECK(Has(r.out, "t                trades every=1 fill_gaps=no cols=10"));

    r = Run(base + "--plugin " + Quote(NQ_TEST_PLUGIN_PATH) + " --format json", tmp);
    CHECK(r.code == 0);
    CHECK(Has(r.out, "\"name\": \"nq_test_plugin\"") && Has(r.out, "\"kind\": \"cumulative\"") && Has(r.out, "\"samplers\": [\n  ]"));

    r = Run(base + "--plugin " + Quote(NQ_FAKE_PLUGIN_PATH), tmp);
    CHECK(r.code == 3 && Has(r.err, "abi hash mismatch"));

    r = Run(base + "--plugin " + Quote(dir / "notes.txt"), tmp);
    CHECK(r.code == 2 && Has(r.err, "dlopen failed"));

    {
        std::ofstream p(dir / "b_test.params");
        p << "min_qty=abc\n";
    }
    r = Run(base + "--plugin-dir " + Quote(dir), tmp);
    CHECK(r.code == 4 && Has(r.err, "nq_init failed"));
    {
        std::ofstream p(dir / "b_test.params");
        p << "broken line\n";
    }
    r = Run(base + "--plugin-dir " + Quote(dir), tmp);
    CHECK(r.code == 6 && Has(r.err, "expected key=value"));
    fs::remove(dir / "b_test.params");

    r = Run(base + "--sampler bad", tmp);
    CHECK(r.code == 5);
    r = Run(base + "--sampler a=time:1s --sampler a=time:2s", tmp);
    CHECK(r.code == 5 && Has(r.err, "duplicate sampler name"));

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
    const fs::path fixture = tmp / "fixture.mbo";
    if (int rc = WriteFixture(fixture)) return rc;
    const fs::path out = tmp / "out";
    const std::string adapter = "--adapter " + Quote(NQ_REPLAY_ADAPTER_PATH) + " --adapter-param path=" + Quote(fixture);

    Result r = Run("run " + adapter + " --plugin " + Quote(NQ_TEST_PLUGIN_PATH) + " --sampler 1s=time:1s --sampler t=trades:1 --out-dir " + Quote(out), tmp);
    CHECK(r.code == 0);
    CHECK(Has(r.err, "feed  events=7 instruments=2 ts_regressions=0 instrument_unknown=0"));
    CHECK(Has(r.err, "adapter_status=0"));
    CHECK(Has(r.err, "1      AAA") && Has(r.err, "2      BBB"));
    CHECK(Has(r.err, "wrote ") && fs::exists(out / "1s.csv") && fs::exists(out / "t.csv"));
    {
        std::ifstream in(out / "1s.csv");
        std::string header, line;
        std::getline(in, header);
        CHECK(header == "instrument,symbol,ts,nq_test_plugin.spread,nq_test_plugin.mid,nq_test_plugin.buy_vol,nq_test_plugin.sell_vol,nq_test_plugin.trades");
        std::vector<std::string> rows;
        while (std::getline(in, line)) rows.push_back(line);
        CHECK(rows.size() == 5);
        CHECK(rows[0].rfind("1,AAA,1000000000,0.2", 0) == 0 && Has(rows[0], ",2,0,1"));
        CHECK(rows[1].rfind("1,AAA,2000000000,", 0) == 0);
        CHECK(rows[2].rfind("1,AAA,3000000000,", 0) == 0);
        CHECK(rows[3].rfind("2,BBB,1000000000,", 0) == 0 && Has(rows[3], "nan"));
        CHECK(rows[4].rfind("2,BBB,2000000000,0.1", 0) == 0);
    }
    {
        std::ifstream in(out / "t.csv");
        std::string header, line;
        std::getline(in, header);
        std::vector<std::string> rows;
        while (std::getline(in, line)) rows.push_back(line);
        CHECK(rows.size() == 1 && rows[0].rfind("1,AAA,500000001,", 0) == 0);
    }

    r = Run("check-feed " + adapter, tmp);
    CHECK(r.code == 0 && Has(r.err, "feed  events=7 instruments=2") && Has(r.err, "trades=1"));

    const fs::path adir = tmp / "adapter";
    fs::create_directories(adir);
    fs::copy_file(NQ_REPLAY_ADAPTER_PATH, adir / "replay.so", fs::copy_options::overwrite_existing);
    { std::ofstream(adir / "replay.params") << "path=" << fixture.string() << "\n"; }
    r = Run("check-feed --adapter " + Quote(adir / "replay.so"), tmp);
    CHECK(r.code == 0 && Has(r.err, "events=7"));

    r = Run("check-feed --adapter " + Quote(NQ_REPLAY_ADAPTER_PATH), tmp);
    CHECK(r.code == 4 && Has(r.err, "nq_adapter_init failed"));
    r = Run("check-feed " + adapter + " --adapter-param novalue", tmp);
    CHECK(r.code == 6);
    r = Run("check-feed --adapter " + Quote(NQ_TEST_PLUGIN_PATH), tmp);
    CHECK(r.code == 2 && Has(r.err, "missing symbol"));

    const fs::path truncated = tmp / "truncated.mbo";
    {
        std::ifstream in(fixture, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::ofstream o(truncated, std::ios::binary);
        o.write(bytes.data(), static_cast<std::streamsize>(bytes.size() - 3));
    }
    r = Run("check-feed --adapter " + Quote(NQ_REPLAY_ADAPTER_PATH) + " --adapter-param path=" + Quote(truncated), tmp);
    CHECK(r.code == 7 && Has(r.err, "events=6") && Has(r.err, "adapter ended with status 2"));
    return 0;
}

int main() {
    const fs::path tmp = fs::temp_directory_path() / "nqbook_cli_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    if (int rc = TestParsing()) return rc;
    if (int rc = TestFiles(tmp)) return rc;
    if (int rc = TestCli(tmp)) return rc;
    if (int rc = TestRunCli(tmp)) return rc;
    fs::remove_all(tmp);
    std::printf("ok\n");
    return 0;
}
