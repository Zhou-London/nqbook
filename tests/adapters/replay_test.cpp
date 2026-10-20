#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nq/adapter_host.hpp>
#include <nq/params.hpp>

#include "support.hpp"

using nqtest::Mbo;

#include <nq/mbo_file.hpp>

namespace fs = std::filesystem;


static std::vector<nq_mbo> Fixture() {
    return {
        Mbo(NQ_ADD, 100, 1, 1, NQ_BID, 1000, 5),
        Mbo(NQ_ADD, 101, 2, 2, NQ_ASK, 2000, 7),
        Mbo(NQ_TRADE, 102, 1, 1, NQ_BID, 1000, 2),
        Mbo(NQ_CANCEL, 103, 2, 2, NQ_ASK, 0, 0),
        Mbo(NQ_CLEAR, 104, 1, 0, NQ_BID, 0, 0),
    };
}

static int WriteFixture(const fs::path& path) {
    using nq::mbo_file::Writer;
    Writer w(path, {Writer::MakeInstrument(1, "AAA", 10, 1), Writer::MakeInstrument(2, "BBB-USD", 5, 100)});
    for (const auto& m : Fixture()) w.Write(m);
    w.Close();
    return 0;
}

static int TestRoundTrip(const fs::path& path) {
    nq::mbo_file::Reader r(path);
    CHECK(r.Instruments().size() == 2);
    CHECK(std::strcmp(r.Instruments()[1].symbol, "BBB-USD") == 0 && r.Instruments()[1].lot_size == 100);
    const auto expected = Fixture();
    nq_mbo m;
    for (const auto& e : expected) {
        CHECK(r.Next(m) == 1);
        CHECK(std::memcmp(&m, &e, sizeof(nq_mbo)) == 0);
    }
    CHECK(r.Next(m) == 0);
    return 0;
}

static std::vector<nq_mbo> Pull(nq::host::AdapterInstance& inst, int& status) {
    std::vector<nq_mbo> out;
    nq_mbo m;
    for (;;) {
        const int rc = inst.Next(m);
        if (rc == NQ_NEXT_EVENT) {
            out.push_back(m);
            continue;
        }
        if (rc == NQ_NEXT_AGAIN) continue;
        status = rc;
        return out;
    }
}

static int TestReplayPlugin(const fs::path& path, const fs::path& tmp) {
    nq::host::AdapterPlugin plugin(NQ_REPLAY_ADAPTER_PATH);
    CHECK(std::strcmp(plugin.Schema().name, "replay") == 0);
    CHECK(plugin.Schema().provides == NQ_REQUIRES_MBO);

    nq::ParamsBuilder pb;
    pb.Set("path", path.string());
    nq::host::AdapterInstance inst = plugin.Create(*pb.Build());
    CHECK(std::strcmp(inst.Instrument(1)->symbol, "AAA") == 0 && inst.Instrument(1)->tick_size == 10);
    CHECK(std::strcmp(inst.Instrument(2)->symbol, "BBB-USD") == 0);
    CHECK(!inst.Instrument(3));

    int status = 0;
    const auto got = Pull(inst, status);
    CHECK(status == NQ_NEXT_END);
    const auto expected = Fixture();
    CHECK(got.size() == expected.size());
    for (std::size_t i = 0; i < got.size(); ++i) CHECK(std::memcmp(&got[i], &expected[i], sizeof(nq_mbo)) == 0);
    nq_mbo again;
    CHECK(inst.Next(again) == NQ_NEXT_END);

    bool threw = false;
    nq::ParamsBuilder missing;
    missing.Set("path", (tmp / "nope.mbo").string());
    try { plugin.Create(*missing.Build()); } catch (const nq::host::PluginInitError&) { threw = true; }
    CHECK(threw);

    threw = false;
    nq::ParamsBuilder nopath;
    try { plugin.Create(*nopath.Build()); } catch (const nq::host::PluginInitError&) { threw = true; }
    CHECK(threw);

    const fs::path junk = tmp / "junk.mbo";
    { std::ofstream(junk) << "this is not an mbo file at all, just text"; }
    threw = false;
    nq::ParamsBuilder bad;
    bad.Set("path", junk.string());
    try { plugin.Create(*bad.Build()); } catch (const nq::host::PluginInitError&) { threw = true; }
    CHECK(threw);

    const fs::path truncated = tmp / "truncated.mbo";
    {
        std::ifstream in(path, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::ofstream out(truncated, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size() - 7));
    }
    nq::ParamsBuilder tp;
    tp.Set("path", truncated.string());
    nq::host::AdapterInstance t = plugin.Create(*tp.Build());
    const auto partial = Pull(t, status);
    CHECK(status == NQ_NEXT_ERROR && partial.size() == expected.size() - 1);
    CHECK(t.Next(again) == NQ_NEXT_ERROR);
    return 0;
}

int main() {
    const fs::path tmp = fs::temp_directory_path() / "nqbook_replay_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    const fs::path path = tmp / "fixture.mbo";
    if (int r = WriteFixture(path)) return r;
    if (int r = TestRoundTrip(path)) return r;
    if (int r = TestReplayPlugin(path, tmp)) return r;
    fs::remove_all(tmp);
    std::printf("ok\n");
    return 0;
}
