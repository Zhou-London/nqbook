#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <Bitmap.h>

namespace nq {

class TimerWheel {
public:
    static constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::max();
    static constexpr std::int64_t kTickNs = 1'000'000;
    static constexpr std::uint32_t kSlotBits = 12;
    static constexpr std::uint32_t kSlots = 1u << kSlotBits;

    TimerWheel() : slots_(kTotalSlots), used_(kTotalSlots) {}

    void WakeBy(std::uint32_t key, std::int64_t due);

    template <typename Wake>
    void AdvanceTo(std::int64_t now, Wake&& wake) {
        while (now >= idle_until_ && CollectDue(now)) {
            for (const Ready& r : ready_) wake(r.key);
        }
    }

    std::int64_t Due(std::uint32_t key) const noexcept { return key < timers_.size() ? timers_[key].due : kNever; }

private:
    static constexpr std::uint32_t kNil = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::uint32_t kTotalSlots = 2 * kSlots;
    static constexpr std::int64_t kTicksPerLap = kSlots;
    static constexpr std::int64_t kWheelTicks = kTicksPerLap * kSlots;
    static constexpr std::int64_t kLastTick = kNever / kTickNs;

    struct Slot {
        std::uint32_t head = kNil;
        std::uint32_t tail = kNil;
    };

    struct Timer {
        std::int64_t due = kNever;
        std::uint32_t slot = 0;
        std::uint32_t prev = kNil;
        std::uint32_t next = kNil;
    };

    struct Ready {
        std::int64_t due;
        std::uint32_t key;
    };

    static std::int64_t TickOf(std::int64_t ns) noexcept { return ns / kTickNs; }
    static std::int64_t LapOf(std::int64_t tick) noexcept { return tick >> kSlotBits; }
    static std::int64_t FirstTickOf(std::int64_t lap) noexcept { return lap << kSlotBits; }
    static std::int64_t TickStartNs(std::int64_t tick) noexcept { return tick > kLastTick ? kNever : tick * kTickNs; }
    static std::int64_t LapStartNs(std::int64_t lap) noexcept {
        return lap > LapOf(kLastTick) ? kNever : TickStartNs(FirstTickOf(lap));
    }

    static std::uint32_t Wrap(std::int64_t position) noexcept { return static_cast<std::uint32_t>(position) & (kSlots - 1); }
    static std::uint32_t TickSlot(std::int64_t tick) noexcept { return Wrap(tick); }
    static std::uint32_t LapSlot(std::int64_t lap) noexcept { return kSlots + Wrap(lap); }

    bool CollectDue(std::int64_t now);
    void SweepUsedSlots(std::uint32_t first, std::uint32_t last, std::int64_t now);
    void Sweep(std::uint32_t slot, std::int64_t now);
    void Place(std::uint32_t key) noexcept;
    void Link(std::uint32_t key, std::uint32_t slot) noexcept;
    void Unlink(std::uint32_t key) noexcept;

    std::int64_t IdleUntil() const noexcept;
    std::int64_t NextUsedTick(std::int64_t from) const noexcept;
    std::int64_t NextUsedLap(std::int64_t from) const noexcept;
    std::optional<std::uint32_t> StepsToUsedSlot(std::uint32_t slot) const noexcept;

    std::vector<Slot> slots_;
    Bitmap used_;
    std::vector<Timer> timers_;
    std::vector<Ready> ready_;
    std::int64_t tick_ = 0;
    std::int64_t idle_until_ = kNever;
};

}  // namespace nq
