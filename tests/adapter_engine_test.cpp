#include <thread>

#include <nq/adapter_host.hpp>
#include <nq/params.hpp>

#include <Adapter/AdapterEngine.h>

#include "support.hpp"

static int TestAdapterEngine() {
    nq::host::AdapterPlugin plugin(NQ_SYNTHETIC_ADAPTER_PATH);
    nq::ParamsBuilder pb;
    pb.Set("count", "100").Set("instruments", "3").Set("again_every", "7");
    nq::AdapterEngine engine(plugin.Create(*pb.Build()), 4);
    CHECK(engine.Queue().capacity() == 4);
    CHECK(!engine.FindInstrument(1));

    engine.Start();
    while (engine.Stalls() == 0) std::this_thread::yield();
    CHECK(engine.Pushed() == 4 && engine.Queue().size() == 4);
    CHECK(engine.FindInstrument(1)->symbol == "SYN1" && engine.FindInstrument(1)->tick_size == 10);
    CHECK(!engine.FindInstrument(4));

    std::uint64_t got = 0;
    std::uint64_t last = 0;
    bool ok = true;
    for (;;) {
        auto m = engine.Queue().try_pop();
        if (m) {
            ++got;
            ok = ok && m->id == last + 1 && m->action == NQ_ADD;
            last = m->id;
            continue;
        }
        if (engine.Ended() && engine.Queue().empty()) break;
        std::this_thread::yield();
    }
    CHECK(ok && got == 100 && engine.Pushed() == 100 && engine.Stalls() >= 1 && engine.Status() == 0);
    CHECK(engine.FindInstrument(1) && engine.FindInstrument(2) && engine.FindInstrument(3));
    engine.Stop();

    nq::ParamsBuilder unknown;
    unknown.Set("count", "2").Set("instruments", "1");
    nq::AdapterEngine lookup(plugin.Create(*unknown.Build()), 8);
    lookup.Start();
    while (!lookup.Ended()) std::this_thread::yield();
    CHECK(lookup.FindInstrument(1) && !lookup.FindInstrument(2));

    nq::ParamsBuilder endless;
    endless.Set("count", "1").Set("endless", "true");
    nq::AdapterEngine blocked(plugin.Create(*endless.Build()), 1);
    blocked.Start();
    while (blocked.Stalls() == 0) std::this_thread::yield();
    CHECK(blocked.Pushed() == 1 && !blocked.Ended());
    blocked.Stop();
    CHECK(blocked.Pushed() == 1 && !blocked.Ended());
    return 0;
}

int main() {
    if (int r = TestAdapterEngine()) return r;
    std::printf("ok\n");
    return 0;
}
