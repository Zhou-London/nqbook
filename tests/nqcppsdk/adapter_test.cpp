#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <nq/adapter_host.hpp>
#include <nq/adapter_testing.hpp>

#include "test_adapter.hpp"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static int TestSandbox() {
    nq::ParamsBuilder pb;
    pb.Set("count", "5").Set("instruments", "2");
    nq::testing::AdapterSandbox<nq_test::Synthetic> sb(pb.Build());
    CHECK(sb.RunToEnd());
    CHECK(sb.Sink().Status() == 0);
    const auto events = sb.Sink().Events();
    CHECK(events.size() == 5);
    CHECK(events[0].ts == 1 && events[0].instrument == 1 && events[0].side == NQ_BID);
    CHECK(events[1].ts == 2 && events[1].instrument == 2 && events[1].side == NQ_ASK);
    CHECK(events[4].price == 1004 && events[4].action == NQ_ADD);
    CHECK(sb.Adapter().FindInstrument(2)->symbol == "SYN2");
    CHECK(sb.Adapter().FindInstrument(3) == nullptr);

    nq::Mbo m(events[1]);
    CHECK(m.side == nq::Side::Ask && m.instrument == 2);
    const nq_mbo back = m.Raw();
    CHECK(std::memcmp(&back, &events[1], sizeof(nq_mbo)) == 0);
    return 0;
}

static int TestStopMidStream() {
    nq::ParamsBuilder pb;
    pb.Set("count", "1").Set("endless", "true").Set("delay_us", "200");
    nq::testing::AdapterSandbox<nq_test::Synthetic> sb(pb.Build());
    sb.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    sb.Stop();
    const std::size_t n = sb.Sink().Count();
    CHECK(n > 0 && !sb.Sink().Ended());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(sb.Sink().Count() == n);
    return 0;
}

static int TestPlugin() {
    nq::host::AdapterPlugin plugin(NQ_TEST_ADAPTER_PATH);
    CHECK(plugin.Schema().abi_hash == NQ_ADAPTER_ABI_HASH);
    CHECK(std::strcmp(plugin.Schema().name, "synthetic") == 0);
    CHECK(plugin.Schema().provides == NQ_REQUIRES_MBO);
    CHECK(std::strcmp(plugin.Schema().sdk_version, NQ_SDK_VERSION) == 0);

    nq::testing::MockSink sink;
    nq::ParamsBuilder pb;
    pb.Set("count", "3").Set("instruments", "2");
    {
        nq::host::AdapterInstance inst = plugin.Create(*pb.Build(), *sink.Raw());
        CHECK(inst.Instrument(1)->tick_size == 10 && std::strcmp(inst.Instrument(1)->symbol, "SYN1") == 0);
        CHECK(!inst.Instrument(9));
        inst.Start();
        CHECK(sink.WaitEnd());
        inst.Stop();
    }
    CHECK(sink.Count() == 3 && sink.Status() == 0);

    nq::ParamsBuilder bad;
    bool threw = false;
    try { plugin.Create(*bad.Build(), *sink.Raw()); } catch (const nq::host::PluginInitError&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { nq::host::AdapterPlugin feature(NQ_TEST_PLUGIN_PATH); } catch (const nq::host::PluginLoadError& e) { threw = std::string(e.what()).find("missing symbol") != std::string::npos; }
    CHECK(threw);

    threw = false;
    try { nq::host::Plugin adapter_as_feature(NQ_TEST_ADAPTER_PATH); } catch (const nq::host::PluginLoadError&) { threw = true; }
    CHECK(threw);
    return 0;
}

static int TestDestroyStopsThread() {
    nq::host::AdapterPlugin plugin(NQ_TEST_ADAPTER_PATH);
    nq::testing::MockSink sink;
    nq::ParamsBuilder pb;
    pb.Set("count", "1").Set("endless", "true").Set("delay_us", "100");
    {
        nq::host::AdapterInstance inst = plugin.Create(*pb.Build(), *sink.Raw());
        inst.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const std::size_t n = sink.Count();
    CHECK(n > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(sink.Count() == n);
    return 0;
}

int main() {
    if (int r = TestSandbox()) return r;
    if (int r = TestStopMidStream()) return r;
    if (int r = TestPlugin()) return r;
    if (int r = TestDestroyStopsThread()) return r;
    std::printf("ok adapter_abi_hash=%llx\n", static_cast<unsigned long long>(NQ_ADAPTER_ABI_HASH));
    return 0;
}
