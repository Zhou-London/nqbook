#include <Clock/TimerWheel.h>

#include <algorithm>
#include <bit>

namespace nq {

void TimerWheel::WakeBy(std::uint32_t key, std::int64_t due) {
    if (key >= timers_.size()) timers_.resize(std::size_t{key} + 1);
    if (due >= timers_[key].due) return;

    if (timers_[key].due != kNever) Unlink(key);
    timers_[key].due = due;
    Place(key);
    idle_until_ = std::min(idle_until_, due);
}

bool TimerWheel::CollectDue(std::int64_t now) {
    ready_.clear();
    const std::int64_t target = std::max(TickOf(now), tick_);

    if (target - tick_ >= kWheelTicks) {
        tick_ = target;
        SweepUsedSlots(0, kTotalSlots - 1, now);
    }
    while (LapOf(tick_) < LapOf(target)) {
        SweepUsedSlots(TickSlot(tick_), kSlots - 1, now);

        std::int64_t lap = LapOf(tick_) + 1;
        if (NextUsedTick(FirstTickOf(lap)) == kNever) lap = std::min(NextUsedLap(lap), LapOf(target));
        tick_ = FirstTickOf(lap);
        Sweep(LapSlot(lap), now);
    }
    SweepUsedSlots(TickSlot(tick_), TickSlot(target), now);
    tick_ = target;
    idle_until_ = IdleUntil();

    const auto earlier = [](const Ready& a, const Ready& b) { return a.due != b.due ? a.due < b.due : a.key < b.key; };
    if (!std::is_sorted(ready_.begin(), ready_.end(), earlier)) std::sort(ready_.begin(), ready_.end(), earlier);
    return !ready_.empty();
}

void TimerWheel::SweepUsedSlots(std::uint32_t first, std::uint32_t last, std::int64_t now) {
    for (std::uint32_t slot = first; (slot = FindUsedSlot(slot, last)) != kNil; ++slot) Sweep(slot, now);
}

void TimerWheel::Sweep(std::uint32_t slot, std::int64_t now) {
    std::uint32_t key = slots_[slot].head;
    slots_[slot] = Slot{};
    MarkEmpty(slot);

    while (key != kNil) {
        Timer& t = timers_[key];
        const std::uint32_t following = t.next;
        if (t.due <= now) {
            ready_.push_back(Ready{t.due, key});
            t.due = kNever;
        } else {
            Place(key);
        }
        key = following;
    }
}

void TimerWheel::Place(std::uint32_t key) noexcept {
    const std::int64_t tick = std::max(TickOf(timers_[key].due), tick_);
    Link(key, tick - tick_ < kTicksPerLap ? TickSlot(tick) : LapSlot(LapOf(tick)));
}

void TimerWheel::Link(std::uint32_t key, std::uint32_t slot) noexcept {
    Timer& t = timers_[key];
    t.slot = slot;
    t.prev = slots_[slot].tail;
    t.next = kNil;
    if (t.prev != kNil) timers_[t.prev].next = key;
    else slots_[slot].head = key;
    slots_[slot].tail = key;
    MarkUsed(slot);
}

void TimerWheel::Unlink(std::uint32_t key) noexcept {
    const Timer& t = timers_[key];
    if (t.prev != kNil) timers_[t.prev].next = t.next;
    else slots_[t.slot].head = t.next;
    if (t.next != kNil) timers_[t.next].prev = t.prev;
    else slots_[t.slot].tail = t.prev;
    if (slots_[t.slot].head == kNil) MarkEmpty(t.slot);
}

std::int64_t TimerWheel::IdleUntil() const noexcept {
    std::int64_t until = std::min(TickStartNs(NextUsedTick(tick_ + 1)), LapStartNs(NextUsedLap(LapOf(tick_) + 1)));
    for (std::uint32_t key = slots_[TickSlot(tick_)].head; key != kNil; key = timers_[key].next) {
        until = std::min(until, timers_[key].due);
    }
    return until;
}

std::int64_t TimerWheel::NextUsedTick(std::int64_t from) const noexcept {
    const std::uint32_t steps = StepsToUsedSlot(TickSlot(from));
    return steps == kNil ? kNever : from + steps;
}

std::int64_t TimerWheel::NextUsedLap(std::int64_t from) const noexcept {
    const std::uint32_t steps = StepsToUsedSlot(LapSlot(from));
    return steps == kNil ? kNever : from + steps;
}

std::uint32_t TimerWheel::StepsToUsedSlot(std::uint32_t slot) const noexcept {
    const std::uint32_t level_first = slot & ~(kSlots - 1);
    const std::uint32_t level_last = level_first + kSlots - 1;

    if (const std::uint32_t ahead = FindUsedSlot(slot, level_last); ahead != kNil) return ahead - slot;
    if (slot == level_first) return kNil;
    if (const std::uint32_t wrapped = FindUsedSlot(level_first, slot - 1); wrapped != kNil) return kSlots + wrapped - slot;
    return kNil;
}

std::uint32_t TimerWheel::FindUsedSlot(std::uint32_t first, std::uint32_t last) const noexcept {
    for (std::uint32_t w = first / 64; w <= last / 64; ++w) {
        std::uint64_t word = used_[w];
        if (w == first / 64) word &= ~std::uint64_t{0} << (first % 64);
        if (w == last / 64) word &= ~std::uint64_t{0} >> (63 - last % 64);
        if (word != 0) return w * 64 + static_cast<std::uint32_t>(std::countr_zero(word));
    }
    return kNil;
}

}  // namespace nq
