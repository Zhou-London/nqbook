#include <cstdio>
#include <cstring>
#include <string>

#include <nq/adapter_host.hpp>
#include <nq/adapter_testing.hpp>
#include <nq/feature_host.hpp>

#include "test_adapter.hpp"

#include "check.hpp"

static int TestSandbox() {
    nq::ParamsBuilder pb;
    pb.Set("count", "5").Set("instruments", "2").Set("again_every", "3");
    nq::testing::AdapterSandbox<nq_test::Synthetic> sb(pb.Build());
    const auto events = sb.Drain();
    CHECK(sb.LastStatus() == nq::Next::End);
    CHECK(events.size() == 5);
    CHECK(events[0].ts == 1 && events[0].instrument == 1 && events[0].side == nq::Side::Bid);
    CHECK(events[1].ts == 2 && events[1].instrument == 2 && events[1].side == nq::Side::Ask);
    CHECK(events[4].price == 1004 && events[4].action == nq::Action::Add);
    CHECK(sb.Adapter().FindInstrument(2)->symbol == "SYN2");
    CHECK(sb.Adapter().FindInstrument(3) == nullptr);

    nq::Mbo m;
    CHECK(sb.Step(m) == nq::Next::End);
    CHECK(sb.Step(m) == nq::Next::End);

    const nq_mbo raw = events[1].Raw();
    CHECK(raw.instrument == 2 && raw.side == NQ_ASK && raw.action == NQ_ADD);
    nq::Mbo back(raw);
    CHECK(back.side == nq::Side::Ask && back.ts == 2);

    nq::ParamsBuilder two;
    two.Set("count", "9");
    nq::testing::AdapterSandbox<nq_test::Synthetic> partial(two.Build());
    CHECK(partial.Drain(4).size() == 4 && partial.LastStatus() == nq::Next::Event);
    CHECK(partial.Drain().size() == 5 && partial.LastStatus() == nq::Next::End);
    return 0;
}

static int TestPlugin() {
    nq::host::AdapterPlugin plugin(NQ_TEST_ADAPTER_PATH);
    CHECK(plugin.Schema().abi_hash == NQ_ADAPTER_ABI_HASH);
    CHECK(std::strcmp(plugin.Schema().name, "synthetic") == 0);
    CHECK(plugin.Schema().provides == NQ_REQUIRES_MBO);
    CHECK(std::strcmp(plugin.Schema().sdk_version, NQ_SDK_VERSION) == 0);

    nq::ParamsBuilder pb;
    pb.Set("count", "3").Set("instruments", "2").Set("again_every", "2");
    nq::host::AdapterInstance inst = plugin.Create(*pb.Build());
    CHECK(inst.Instrument(1)->tick_size == 10 && std::strcmp(inst.Instrument(1)->symbol, "SYN1") == 0);
    CHECK(!inst.Instrument(9));

    nq_mbo m;
    int events = 0;
    int again = 0;
    for (;;) {
        const int rc = inst.Next(m);
        if (rc == NQ_NEXT_EVENT) {
            ++events;
            CHECK(m.id == static_cast<std::uint64_t>(events));
            continue;
        }
        if (rc == NQ_NEXT_AGAIN) {
            ++again;
            continue;
        }
        CHECK(rc == NQ_NEXT_END);
        break;
    }
    CHECK(events == 3 && again >= 1);
    CHECK(inst.Next(m) == NQ_NEXT_END);

    nq::ParamsBuilder bad;
    bool threw = false;
    try { plugin.Create(*bad.Build()); } catch (const nq::host::PluginInitError&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { nq::host::AdapterPlugin feature(NQ_TEST_PLUGIN_PATH); } catch (const nq::host::PluginLoadError& e) { threw = std::string(e.what()).find("missing symbol") != std::string::npos; }
    CHECK(threw);

    threw = false;
    try { nq::host::Plugin adapter_as_feature(NQ_TEST_ADAPTER_PATH); } catch (const nq::host::PluginLoadError&) { threw = true; }
    CHECK(threw);
    return 0;
}

int main() {
    if (int r = TestSandbox()) return r;
    if (int r = TestPlugin()) return r;
    std::printf("ok adapter_abi_hash=%llx\n", static_cast<unsigned long long>(NQ_ADAPTER_ABI_HASH));
    return 0;
}
