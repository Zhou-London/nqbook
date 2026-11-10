#include <cstdint>
#include <vector>

#include <Clock/TimerQueue.h>
#include <Feature/Trigger.h>

#include "support.hpp"

using Times = std::vector<std::int64_t>;
using Keys = std::vector<std::uint32_t>;

static int TestTimerQueue() {
    nq::TimerQueue timers;
    Keys woken;
    auto wake = [&](std::uint32_t key) { woken.push_back(key); };

    timers.WakeBy(2, 5);
    timers.WakeBy(1, 3);
    timers.WakeBy(0, 3);
    timers.AdvanceTo(2, wake);
    CHECK(woken.empty());
    timers.AdvanceTo(3, wake);
    CHECK((woken == Keys{0, 1}) && timers.Due(0) == nq::TimerQueue::kNever);
    timers.AdvanceTo(10, wake);
    CHECK((woken == Keys{0, 1, 2}));

    woken.clear();
    timers.WakeBy(0, 7);
    timers.WakeBy(0, 4);
    timers.WakeBy(0, 9);
    CHECK(timers.Due(0) == 4);
    timers.AdvanceTo(100, wake);
    CHECK((woken == Keys{0}));

    timers.WakeBy(3, nq::TimerQueue::kNever);
    CHECK(timers.Due(3) == nq::TimerQueue::kNever && timers.Due(99) == nq::TimerQueue::kNever);

    woken.clear();
    timers.WakeBy(1, 10);
    timers.AdvanceTo(10, [&](std::uint32_t key) {
        woken.push_back(key);
        timers.WakeBy(key, 20);
    });
    CHECK((woken == Keys{1}) && timers.Due(1) == 20);

    woken.clear();
    nq::TimerQueue wide;
    for (std::uint32_t key : {130u, 5u, 64u, 63u, 200u}) wide.WakeBy(key, 50);
    wide.WakeBy(7, 40);
    wide.WakeBy(200, 45);
    wide.AdvanceTo(60, [&](std::uint32_t key) { woken.push_back(key); });
    CHECK((woken == Keys{7, 200, 5, 63, 64, 130}));

    woken.clear();
    wide.WakeBy(64, 70);
    wide.WakeBy(3, 70);
    wide.AdvanceTo(69, [&](std::uint32_t key) { woken.push_back(key); });
    CHECK(woken.empty());
    wide.AdvanceTo(70, [&](std::uint32_t key) { woken.push_back(key); });
    CHECK((woken == Keys{3, 64}));
    return 0;
}

static int TestBarTrigger() {
    Times out;
    auto emit = [&](std::int64_t at) { out.push_back(at); };

    nq::BarTrigger bars(10, false);
    CHECK(bars.Due() == nq::TimerQueue::kNever);
    bars.Advance(3, emit);
    CHECK(bars.Due() == nq::TimerQueue::kNever);
    bars.OnEvent(3, NQ_ADD, emit);
    CHECK(bars.Due() == 10 && out.empty());
    bars.Advance(12, emit);
    CHECK((out == Times{10}) && bars.Due() == nq::TimerQueue::kNever);
    bars.Advance(25, emit);
    CHECK((out == Times{10}));
    bars.OnEvent(25, NQ_ADD, emit);
    CHECK(bars.Due() == 30);
    bars.Close(emit);
    CHECK((out == Times{10, 30}));

    out.clear();
    nq::BarTrigger filled(10, true);
    filled.Advance(3, emit);
    CHECK(filled.Due() == 10);
    filled.Advance(34, emit);
    CHECK((out == Times{10, 20, 30}) && filled.Due() == 40);
    filled.Close(emit);
    filled.Close(emit);
    CHECK((out == Times{10, 20, 30, 40, 40}));
    return 0;
}

static int TestCountTrigger() {
    Times out;
    auto emit = [&](std::int64_t at) { out.push_back(at); };

    nq::CountTrigger events(3, false);
    for (std::int64_t ts = 1; ts <= 7; ++ts) events.OnEvent(ts, NQ_ADD, emit);
    CHECK((out == Times{3, 6}) && events.Due() == nq::TimerQueue::kNever);
    events.Advance(100, emit);
    CHECK((out == Times{3, 6}));
    events.Close(emit);
    events.Close(emit);
    CHECK((out == Times{3, 6, 7}));

    out.clear();
    nq::CountTrigger trades(2, true);
    trades.OnEvent(1, NQ_TRADE, emit);
    trades.OnEvent(2, NQ_ADD, emit);
    trades.OnEvent(3, NQ_TRADE, emit);
    trades.OnEvent(4, NQ_TRADE, emit);
    CHECK((out == Times{3}));
    trades.Close(emit);
    CHECK(out.size() == 2);
    return 0;
}

int main() {
    if (int r = TestTimerQueue()) return r;
    if (int r = TestBarTrigger()) return r;
    if (int r = TestCountTrigger()) return r;
    std::printf("ok\n");
    return 0;
}
