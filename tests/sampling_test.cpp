#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <utility>
#include <vector>

#include <Clock/TimerWheel.h>
#include <Feature/Trigger.h>

#include "support.hpp"

using Times = std::vector<std::int64_t>;
using Keys = std::vector<std::uint32_t>;

constexpr bool kTrade = true;
constexpr bool kNotTrade = false;

static int TestTimerWheel() {
    nq::TimerWheel timers;
    Keys woken;
    auto wake = [&](std::uint32_t key) { woken.push_back(key); };

    timers.WakeBy(2, 5);
    timers.WakeBy(1, 3);
    timers.WakeBy(0, 3);
    timers.AdvanceTo(2, wake);
    CHECK(woken.empty());
    timers.AdvanceTo(3, wake);
    CHECK((woken == Keys{0, 1}) && timers.Due(0) == nq::TimerWheel::kNever);
    timers.AdvanceTo(10, wake);
    CHECK((woken == Keys{0, 1, 2}));

    woken.clear();
    timers.WakeBy(0, 7);
    timers.WakeBy(0, 4);
    timers.WakeBy(0, 9);
    CHECK(timers.Due(0) == 4);
    timers.AdvanceTo(100, wake);
    CHECK((woken == Keys{0}));

    timers.WakeBy(3, nq::TimerWheel::kNever);
    CHECK(timers.Due(3) == nq::TimerWheel::kNever && timers.Due(99) == nq::TimerWheel::kNever);

    woken.clear();
    timers.WakeBy(1, 10);
    timers.AdvanceTo(10, [&](std::uint32_t key) {
        woken.push_back(key);
        timers.WakeBy(key, 20);
    });
    CHECK((woken == Keys{1}) && timers.Due(1) == 20);

    woken.clear();
    nq::TimerWheel wide;
    for (std::uint32_t key : {130u, 5u, 64u, 63u, 200u}) wide.WakeBy(key, 50);
    wide.WakeBy(7, 40);
    wide.WakeBy(200, 45);
    wide.AdvanceTo(60, wake);
    CHECK((woken == Keys{7, 200, 5, 63, 64, 130}));

    woken.clear();
    wide.WakeBy(64, 70);
    wide.WakeBy(3, 70);
    wide.AdvanceTo(69, wake);
    CHECK(woken.empty());
    wide.AdvanceTo(70, wake);
    CHECK((woken == Keys{3, 64}));
    return 0;
}

static int TestTimerWheelLaps() {
    const std::int64_t ms = nq::TimerWheel::kTickNs;
    const std::int64_t lap = ms * static_cast<std::int64_t>(nq::TimerWheel::kSlots);
    const std::int64_t t0 = 1'700'000'000'000 * ms;
    nq::TimerWheel wheel;
    Keys woken;
    auto wake = [&](std::uint32_t key) { woken.push_back(key); };

    wheel.AdvanceTo(t0, wake);
    wheel.WakeBy(0, t0 + 3 * lap + 5 * ms);
    wheel.WakeBy(1, t0 + 5 * ms);
    wheel.WakeBy(2, t0 + 5 * ms + 400);
    wheel.AdvanceTo(t0 + 5 * ms - 1, wake);
    CHECK(woken.empty());
    wheel.AdvanceTo(t0 + 5 * ms + 399, wake);
    CHECK((woken == Keys{1}));
    wheel.AdvanceTo(t0 + 5 * ms + 400, wake);
    CHECK((woken == Keys{1, 2}));
    for (std::int64_t t = t0 + 6 * ms; t < t0 + 3 * lap + 5 * ms; t += 700 * ms) wheel.AdvanceTo(t, wake);
    CHECK((woken == Keys{1, 2}) && wheel.Due(0) == t0 + 3 * lap + 5 * ms);
    wheel.AdvanceTo(t0 + 3 * lap + 5 * ms, wake);
    CHECK((woken == Keys{1, 2, 0}));

    woken.clear();
    const std::int64_t t1 = t0 + 4 * lap;
    wheel.AdvanceTo(t1, wake);
    wheel.WakeBy(4, t1 + 2 * lap);
    wheel.WakeBy(3, t1 + 10 * ms);
    wheel.AdvanceTo(t1 + 100 * lap, wake);
    CHECK((woken == Keys{3, 4}));

    woken.clear();
    const std::int64_t t2 = t1 + 100 * lap;
    wheel.WakeBy(5, t2 - 50 * ms);
    wheel.AdvanceTo(t2 - 60 * ms, wake);
    CHECK(woken.empty());
    wheel.AdvanceTo(t2 - 50 * ms, wake);
    CHECK((woken == Keys{5}));

    woken.clear();
    wheel.WakeBy(6, t2 + 500 * ms);
    wheel.WakeBy(6, t2 + 20 * ms);
    wheel.AdvanceTo(t2 + 20 * ms, wake);
    wheel.AdvanceTo(t2 + 600 * ms, wake);
    CHECK((woken == Keys{6}));

    woken.clear();
    const std::int64_t hour = 3'600'000 * ms;
    const std::int64_t t3 = t2 + 700 * ms;
    wheel.AdvanceTo(t3, wake);
    wheel.WakeBy(7, t3 + 3 * hour);
    wheel.WakeBy(8, t3 + 6 * hour);
    wheel.WakeBy(9, t3 + 10'000 * ms);
    for (std::int64_t t = t3; t < t3 + 3 * hour; t += 420'000 * ms) wheel.AdvanceTo(t, wake);
    CHECK((woken == Keys{9}));
    wheel.AdvanceTo(t3 + 3 * hour - 1, wake);
    CHECK((woken == Keys{9}));
    wheel.AdvanceTo(t3 + 3 * hour, wake);
    CHECK((woken == Keys{9, 7}));
    wheel.AdvanceTo(t3 + 6 * hour - 1, wake);
    CHECK((woken == Keys{9, 7}) && wheel.Due(8) == t3 + 6 * hour);
    wheel.AdvanceTo(t3 + 30 * hour, wake);
    CHECK((woken == Keys{9, 7, 8}));
    return 0;
}

static int TestTimerWheelAgainstModel() {
    const std::int64_t ms = nq::TimerWheel::kTickNs;
    std::mt19937_64 rng(7);
    auto pick = [&](std::int64_t below) { return static_cast<std::int64_t>(rng() % static_cast<std::uint64_t>(below)); };

    const std::int64_t reach[] = {9000 * ms, 7'200'000 * ms, 43'200'000 * ms};

    for (int round = 0; round < 300; ++round) {
        const std::int64_t far = reach[round % 3];
        nq::TimerWheel wheel;
        std::map<std::uint32_t, std::int64_t> model;
        std::int64_t now = 1'700'000'000'000 * ms;
        for (int op = 0; op < 500; ++op) {
            if (pick(4) != 0) {
                const auto key = static_cast<std::uint32_t>(pick(64));
                const std::int64_t due = now + pick(pick(3) != 0 ? far : 50 * ms) - (pick(8) == 0 ? 100 * ms : 0);
                wheel.WakeBy(key, due);
                const auto it = model.find(key);
                if (it == model.end() || due < it->second) model[key] = due;
                continue;
            }
            now += pick(pick(8) != 0 ? 3 * ms : far / 2);
            if (pick(16) == 0) now -= 20 * ms;

            std::vector<std::pair<std::int64_t, std::uint32_t>> due_now;
            for (auto it = model.begin(); it != model.end();) {
                if (it->second <= now) {
                    due_now.emplace_back(it->second, it->first);
                    it = model.erase(it);
                } else {
                    ++it;
                }
            }
            std::sort(due_now.begin(), due_now.end());
            Keys expected;
            for (const auto& d : due_now) expected.push_back(d.second);

            Keys woken;
            wheel.AdvanceTo(now, [&](std::uint32_t key) { woken.push_back(key); });
            CHECK(woken == expected);
            for (const auto& [key, due] : model) CHECK(wheel.Due(key) == due);
        }
    }
    return 0;
}

static int TestBarTrigger() {
    Times out;
    auto emit = [&](std::int64_t at) { out.push_back(at); };

    nq::BarTrigger bars({10, false});
    CHECK(bars.Due() == nq::TimerWheel::kNever);
    bars.Advance(3, emit);
    CHECK(bars.Due() == nq::TimerWheel::kNever);
    bars.OnEvent(3, kNotTrade, emit);
    CHECK(bars.Due() == 10 && out.empty());
    bars.Advance(12, emit);
    CHECK((out == Times{10}) && bars.Due() == nq::TimerWheel::kNever);
    bars.Advance(25, emit);
    CHECK((out == Times{10}));
    bars.OnEvent(25, kNotTrade, emit);
    CHECK(bars.Due() == 30);
    bars.Close(emit);
    CHECK((out == Times{10, 30}));

    out.clear();
    nq::BarTrigger filled({10, true});
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
    for (std::int64_t ts = 1; ts <= 7; ++ts) events.OnEvent(ts, kNotTrade, emit);
    CHECK((out == Times{3, 6}) && events.Due() == nq::TimerWheel::kNever);
    events.Advance(100, emit);
    CHECK((out == Times{3, 6}));
    events.Close(emit);
    events.Close(emit);
    CHECK((out == Times{3, 6, 7}));

    out.clear();
    nq::CountTrigger trades(2, true);
    trades.OnEvent(1, kTrade, emit);
    trades.OnEvent(2, kNotTrade, emit);
    trades.OnEvent(3, kTrade, emit);
    trades.OnEvent(4, kTrade, emit);
    CHECK((out == Times{3}));
    trades.Close(emit);
    CHECK(out.size() == 2);
    return 0;
}

int main() {
    if (int r = TestTimerWheel()) return r;
    if (int r = TestTimerWheelLaps()) return r;
    if (int r = TestTimerWheelAgainstModel()) return r;
    if (int r = TestBarTrigger()) return r;
    if (int r = TestCountTrigger()) return r;
    std::printf("ok\n");
    return 0;
}
