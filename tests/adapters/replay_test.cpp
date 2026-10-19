#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nq/adapter_host.hpp>
#include <nq/adapter_testing.hpp>

#include "../../adapters/replay/mbo_file.hpp"

namespace fs = std::filesystem;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static nq_mbo Event(std::int64_t ts, std::uint64_t id, std::uint32_t instrument, std::uint8_t side,
                    std::uint8_t action, std::int64_t price, std::int64_t qty) {
    return nq_mbo{ts, id, price, qty, instrument, side, action};
}

static std::vector<nq_mbo> Fixture() {
    return {
        Event(100, 1, 1, NQ_BID, NQ_ADD, 1000, 5),
        Event(101, 2, 2, NQ_ASK, NQ_ADD, 2000, 7),
        Event(102, 1, 1, NQ_BID, NQ_TRADE, 1000, 2),
        Event(103, 2, 2, NQ_ASK, NQ_CANCEL, 0, 0),
        Event(104, 0, 1, NQ_BID, NQ_CLEAR, 0, 0),
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

static int TestReplayPlugin(const fs::path& path, const fs::path& tmp) {
    nq::host::AdapterPlugin plugin(NQ_REPLAY_ADAPTER_PATH);
    CHECK(std::strcmp(plugin.Schema().name, "replay") == 0);
    CHECK(plugin.Schema().provides == NQ_REQUIRES_MBO);

    nq::testing::MockSink sink;
    nq::ParamsBuilder pb;
    pb.Set("path", path.string());
    {
        nq::host::AdapterInstance inst = plugin.Create(*pb.Build(), *sink.Raw());
        CHECK(std::strcmp(inst.Instrument(1)->symbol, "AAA") == 0 && inst.Instrument(1)->tick_size == 10);
        CHECK(std::strcmp(inst.Instrument(2)->symbol, "BBB-USD") == 0);
        CHECK(!inst.Instrument(3));
        inst.Start();
        CHECK(sink.WaitEnd());
    }
    CHECK(sink.Status() == 0);
    const auto got = sink.Events();
    const auto expected = Fixture();
    CHECK(got.size() == expected.size());
    for (std::size_t i = 0; i < got.size(); ++i) CHECK(std::memcmp(&got[i], &expected[i], sizeof(nq_mbo)) == 0);

    bool threw = false;
    nq::ParamsBuilder missing;
    missing.Set("path", (tmp / "nope.mbo").string());
    try { plugin.Create(*missing.Build(), *sink.Raw()); } catch (const nq::host::PluginInitError&) { threw = true; }
    CHECK(threw);

    threw = false;
    nq::ParamsBuilder nopath;
    try { plugin.Create(*nopath.Build(), *sink.Raw()); } catch (const nq::host::PluginInitError&) { threw = true; }
    CHECK(threw);

    const fs::path junk = tmp / "junk.mbo";
    { std::ofstream(junk) << "this is not an mbo file at all, just text"; }
    threw = false;
    nq::ParamsBuilder bad;
    bad.Set("path", junk.string());
    try { plugin.Create(*bad.Build(), *sink.Raw()); } catch (const nq::host::PluginInitError&) { threw = true; }
    CHECK(threw);

    const fs::path truncated = tmp / "truncated.mbo";
    {
        std::ifstream in(path, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::ofstream out(truncated, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size() - 7));
    }
    nq::testing::MockSink sink2;
    nq::ParamsBuilder tp;
    tp.Set("path", truncated.string());
    {
        nq::host::AdapterInstance inst = plugin.Create(*tp.Build(), *sink2.Raw());
        inst.Start();
        CHECK(sink2.WaitEnd());
    }
    CHECK(sink2.Status() == 2 && sink2.Count() == expected.size() - 1);
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
